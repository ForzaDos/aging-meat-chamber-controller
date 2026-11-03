#include "pesaje.h"
#if USAR_BALANZA
#include "balanza.h"
#include <LittleFS.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <time.h>

static const uint8_t N = BALANZAS_MAX;
struct Punto { uint32_t t; int32_t dg; };   // epoch UTC y peso en décimas de gramo

// Un archivo por balanza: la 0 conserva el histórico de siempre.
static void archivo(uint8_t i, char* b, size_t tam) {
  if (i == 0) strlcpy(b, "/pesaje.bin", tam);
  else        snprintf(b, tam, "/pesaje%u.bin", i);
}

static SemaphoreHandle_t mtx = nullptr;     // seguimientos y archivos (los usan el loop() y la web)
static Preferences pp[BALANZAS_MAX];
static PesajeEstado S[BALANZAS_MAX];        // lo que se muestra (bajo mtx)

// Promedio del intervalo en curso (solo el loop()).
static uint32_t slotAct[BALANZAS_MAX] = {0};
static double   acum[BALANZAS_MAX] = {0};
static uint32_t nAcum[BALANZAS_MAX] = {0};
// Último peso conocido.
static float    ultPeso[BALANZAS_MAX];
static uint32_t ultPesoMs[BALANZAS_MAX] = {0};

struct Candado { Candado() { xSemaphoreTake(mtx, portMAX_DELAY); } ~Candado() { xSemaphoreGive(mtx); } };

static uint32_t ahoraEpoch() { const time_t t = time(nullptr); return t > 1700000000 ? (uint32_t)t : 0; }

static uint16_t contarPuntos(uint8_t i) {
  char ruta[20];
  archivo(i, ruta, sizeof(ruta));
  File f = LittleFS.open(ruta, "r");
  if (!f) return 0;
  const size_t n = f.size() / sizeof(Punto);
  f.close();
  return (uint16_t)min(n, (size_t)65535);
}

static bool agregarPunto(uint8_t i, uint32_t t, float g) {
  if (S[i].puntos >= PESAJE_MAX_PUNTOS) return false;
  char ruta[20];
  archivo(i, ruta, sizeof(ruta));
  File f = LittleFS.open(ruta, "a");
  if (!f) return false;
  const Punto p = { t, (int32_t)lroundf(g * 10.0f) };
  const bool ok = f.write((const uint8_t*)&p, sizeof(p)) == sizeof(p);
  f.close();
  if (ok) S[i].puntos++;
  return ok;
}

// Punto i del archivo (abierto). false si no existe.
static bool leerPunto(File& f, size_t i, Punto& p) {
  if (!f.seek(i * sizeof(Punto))) return false;
  return f.read((uint8_t*)&p, sizeof(p)) == sizeof(p);
}

static void guardarConfig(uint8_t i) {
  pp[i].putBool("act", S[i].activo);
  pp[i].putFloat("p0", S[i].p0);
  pp[i].putFloat("obj", S[i].objetivo);
  pp[i].putULong("ini", S[i].inicio);
  pp[i].putString("tanda", S[i].tanda);
  pp[i].putString("nom", S[i].nombre);
  pp[i].putBool("pau", S[i].pausado);
  pp[i].putULong("pauD", S[i].pausaDesde);
}

// Recalcula peso, merma y estimación (con mtx tomado).
static void recalcular(uint8_t i) {
  char ruta[20];
  archivo(i, ruta, sizeof(ruta));
  const uint32_t ahora = ahoraEpoch();
  S[i].enVivo = !S[i].pausado && !isnan(ultPeso[i]) && millis() - ultPesoMs[i] < 5000;
  S[i].peso = S[i].enVivo ? ultPeso[i] : NAN;
  uint32_t tPeso = ahora;                        // momento del peso que se muestra

  File f = LittleFS.open(ruta, "r");
  const size_t n = f ? f.size() / sizeof(Punto) : 0;
  Punto ult;
  if (isnan(S[i].peso) && n && leerPunto(f, n - 1, ult)) { S[i].peso = ult.dg / 10.0f; tPeso = ult.t; }   // en pausa o sin balanza: el último registrado

  S[i].merma = (!isnan(S[i].peso) && S[i].p0 > 0) ? (S[i].p0 - S[i].peso) / S[i].p0 * 100.0f : NAN;
  S[i].dias  = (ahora && S[i].inicio && ahora > S[i].inicio) ? (ahora - S[i].inicio) / 86400.0f : 0;
  S[i].ritmo = NAN;
  S[i].falta = NAN;

  // Referencia para el ritmo: el punto de hace PESAJE_VENTANA_H horas (o el inicio, si es más nuevo).
  if (tPeso && !isnan(S[i].merma) && n) {
    const uint32_t buscado = tPeso - PESAJE_VENTANA_H * 3600UL;
    size_t lo = 0, hi = n - 1;                   // primer punto con t >= buscado
    Punto p;
    while (lo < hi) {
      const size_t m = (lo + hi) / 2;
      if (leerPunto(f, m, p) && p.t < buscado) lo = m + 1; else hi = m;
    }
    if (leerPunto(f, lo, p) && tPeso > p.t && tPeso - p.t >= PESAJE_MIN_H * 3600UL) {
      const float mRef = (S[i].p0 - p.dg / 10.0f) / S[i].p0 * 100.0f;
      S[i].ritmo = (S[i].merma - mRef) / ((tPeso - p.t) / 86400.0f);
    }
  }
  if (f) f.close();

  if (!isnan(S[i].merma) && S[i].objetivo > 0) {
    if (S[i].merma >= S[i].objetivo)                  S[i].falta = 0;
    else if (!isnan(S[i].ritmo) && S[i].ritmo > 0.01f) S[i].falta = (S[i].objetivo - S[i].merma) / S[i].ritmo;
  }
}

void pesajeInit() {
  mtx = xSemaphoreCreateMutex();
  char ns[14];
  for (uint8_t i = 0; i < N; i++) {
    if (i == 0) strlcpy(ns, "pesaje", sizeof(ns));
    else        snprintf(ns, sizeof(ns), "pesaje%u", i);
    pp[i].begin(ns, false);
    S[i].activo   = pp[i].getBool("act", false);
    S[i].p0       = pp[i].getFloat("p0", 0);
    S[i].objetivo = pp[i].getFloat("obj", 30);
    S[i].inicio   = pp[i].getULong("ini", 0);
    S[i].pausado  = S[i].activo && pp[i].getBool("pau", false);
    S[i].pausaDesde = S[i].pausado ? pp[i].getULong("pauD", 0) : 0;
    strlcpy(S[i].tanda,  pp[i].getString("tanda", "").c_str(), sizeof(S[i].tanda));
    strlcpy(S[i].nombre, pp[i].getString("nom", "").c_str(),   sizeof(S[i].nombre));
    S[i].puntos = contarPuntos(i);
    ultPeso[i] = NAN;
  }
  Candado c;
  for (uint8_t i = 0; i < N; i++) recalcular(i);
}

void pesajeLoop() {
  static uint32_t ultSeg = 0, ultCalc = 0;
  const uint32_t ms = millis();
  if (ms - ultSeg < 1000) return;
  ultSeg = ms;

  const uint32_t ahora = ahoraEpoch();
  bool cambio = false;
  Candado c;
  for (uint8_t i = 0; i < N; i++) {
    const BalanzaEstado b = balanzaEstado(i);
    bool valido = b.hayPeso && !(b.flags & BAL_SOBRECARGA);

    // Casi sin peso con un seguimiento en curso: la pieza no está colgada. No se registra ni se
    // muestra como merma (sería ~100 %).
    S[i].descolgada = valido && S[i].activo && S[i].p0 > 0 && b.gramos < S[i].p0 * PESAJE_DESCOLGADA;
    if (S[i].descolgada) valido = false;
    if (valido) { ultPeso[i] = b.gramos; ultPesoMs[i] = ms; }
    if (S[i].pausado) { slotAct[i] = 0; acum[i] = 0; nAcum[i] = 0; }   // en pausa no se junta nada
    else if (S[i].activo && ahora) {
      const uint32_t slot = ahora / PESAJE_INTERVALO_S;
      if (slotAct[i] && slot != slotAct[i] && nAcum[i]) {              // terminó un intervalo: se guarda el promedio
        agregarPunto(i, slotAct[i] * PESAJE_INTERVALO_S + PESAJE_INTERVALO_S / 2, (float)(acum[i] / nAcum[i]));
        cambio = true;
      }
      if (slot != slotAct[i]) { slotAct[i] = slot; acum[i] = 0; nAcum[i] = 0; }
      if (valido && (b.flags & BAL_ESTABLE)) { acum[i] += b.gramos; nAcum[i]++; }   // solo con el peso quieto
    }
  }
  if (cambio || ms - ultCalc >= 5000) {
    ultCalc = ms;
    for (uint8_t i = 0; i < N; i++) recalcular(i);
  }
}

PesajeEstado pesajeEstado(uint8_t i) {
  if (i >= N) i = 0;
  Candado c;
  return S[i];
}

bool pesajeIniciar(float p0, float objetivo, uint32_t inicio, const char* tanda, const char* nombre, uint8_t i) {
  if (i >= N) i = 0;
  const float tope = balanzaCapacidad(i);
  if (!(p0 >= 1 && p0 <= tope) || !(objetivo >= 1 && objetivo <= 80)) return false;
  const uint32_t ahora = ahoraEpoch();
  if (!inicio) inicio = ahora;
  if (!inicio) return false;
  if (ahora) {                                    // fecha de ingreso razonable: ni en el futuro ni de hace más de 2 años
    if (inicio > ahora) inicio = ahora;
    else if (ahora - inicio > 2UL * 365 * 86400) inicio = ahora - 2UL * 365 * 86400;
  }
  char ruta[20];
  archivo(i, ruta, sizeof(ruta));
  Candado c;
  LittleFS.remove(ruta);
  S[i].puntos = 0;
  S[i].activo = true;
  S[i].p0 = p0;
  S[i].objetivo = objetivo;
  S[i].inicio = inicio;
  strlcpy(S[i].tanda,  tanda  ? tanda  : "", sizeof(S[i].tanda));
  strlcpy(S[i].nombre, nombre ? nombre : "", sizeof(S[i].nombre));
  S[i].pausado = false;
  S[i].pausaDesde = 0;
  agregarPunto(i, inicio, p0);                    // el punto de partida
  slotAct[i] = 0; acum[i] = 0; nAcum[i] = 0;
  guardarConfig(i);
  recalcular(i);
  return true;
}

bool pesajeEditar(float p0, float objetivo, const char* tanda, const char* nombre, uint8_t i) {
  if (i >= N) i = 0;
  const float tope = balanzaCapacidad(i);
  if (!isnan(p0) && !(p0 >= 1 && p0 <= tope)) return false;
  if (!isnan(objetivo) && !(objetivo >= 1 && objetivo <= 80)) return false;
  Candado c;
  if (!isnan(p0)) S[i].p0 = p0;
  if (!isnan(objetivo)) S[i].objetivo = objetivo;
  if (tanda)  strlcpy(S[i].tanda,  tanda,  sizeof(S[i].tanda));
  if (nombre) strlcpy(S[i].nombre, nombre, sizeof(S[i].nombre));
  guardarConfig(i);
  recalcular(i);
  return true;
}

void pesajeTerminar(uint8_t i) {
  if (i >= N) i = 0;
  Candado c;
  S[i].activo = false;
  S[i].pausado = false;
  S[i].pausaDesde = 0;
  guardarConfig(i);
}

bool pesajePausar(bool pausar, uint8_t i) {
  if (i >= N) i = 0;
  Candado c;
  if (!S[i].activo) return false;
  if (S[i].pausado == pausar) return true;
  S[i].pausado = pausar;
  S[i].pausaDesde = pausar ? ahoraEpoch() : 0;
  slotAct[i] = 0; acum[i] = 0; nAcum[i] = 0;      // el intervalo a medias se descarta (pudo incluir el descuelgue)
  guardarConfig(i);
  recalcular(i);
  Serial.printf("Seguimiento %u %s\n", i, pausar ? "en pausa" : "retomado");
  return true;
}

#define JSON_PUNTOS 600
size_t pesajeJsonTam(uint8_t i) { (void)i; return JSON_PUNTOS * 26 + 160; }

void pesajeJson(Print& out, uint8_t i) {
  if (i >= N) i = 0;
  char ruta[20];
  archivo(i, ruta, sizeof(ruta));
  Candado c;
  out.printf("{\"p0\":%.1f,\"obj\":%.1f,\"ini\":%lu,\"pts\":[", S[i].p0, S[i].objetivo, (unsigned long)S[i].inicio);
  File f = LittleFS.open(ruta, "r");
  if (f) {
    const size_t n = f.size() / sizeof(Punto);
    const size_t paso = n > JSON_PUNTOS ? (n + JSON_PUNTOS - 1) / JSON_PUNTOS : 1;
    bool primero = true;
    Punto p;
    for (size_t k = 0; k < n; k += paso) {
      // el último punto siempre va (es el más nuevo)
      if (!leerPunto(f, k, p)) break;
      out.printf("%s[%lu,%.1f]", primero ? "" : ",", (unsigned long)p.t, p.dg / 10.0f);
      primero = false;
    }
    if (n && (n - 1) % paso && leerPunto(f, n - 1, p)) out.printf(",[%lu,%.1f]", (unsigned long)p.t, p.dg / 10.0f);
    f.close();
  }
  out.print("]}");
}
#endif
