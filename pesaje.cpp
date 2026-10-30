#include "pesaje.h"
#if USAR_BALANZA
#include "balanza.h"
#include <LittleFS.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <time.h>

static const char* ARCH = "/pesaje.bin";
struct Punto { uint32_t t; int32_t dg; };   // epoch UTC y peso en décimas de gramo

static SemaphoreHandle_t mtx = nullptr;     // seguimiento y archivo (los usan el loop() y la web)
static Preferences pp;
static PesajeEstado S = {};                 // lo que se muestra (bajo mtx)

// Promedio del intervalo en curso (solo el loop()).
static uint32_t slotAct = 0;
static double   acum = 0;
static uint32_t nAcum = 0;
// Último peso conocido.
static float    ultPeso = NAN;
static uint32_t ultPesoMs = 0;

struct Candado { Candado() { xSemaphoreTake(mtx, portMAX_DELAY); } ~Candado() { xSemaphoreGive(mtx); } };

static uint32_t ahoraEpoch() { const time_t t = time(nullptr); return t > 1700000000 ? (uint32_t)t : 0; }

static uint16_t contarPuntos() {
  File f = LittleFS.open(ARCH, "r");
  if (!f) return 0;
  const size_t n = f.size() / sizeof(Punto);
  f.close();
  return (uint16_t)min(n, (size_t)65535);
}

static bool agregarPunto(uint32_t t, float g) {
  if (S.puntos >= PESAJE_MAX_PUNTOS) return false;
  File f = LittleFS.open(ARCH, "a");
  if (!f) return false;
  const Punto p = { t, (int32_t)lroundf(g * 10.0f) };
  const bool ok = f.write((const uint8_t*)&p, sizeof(p)) == sizeof(p);
  f.close();
  if (ok) S.puntos++;
  return ok;
}

// Punto i del archivo (abierto). false si no existe.
static bool leerPunto(File& f, size_t i, Punto& p) {
  if (!f.seek(i * sizeof(Punto))) return false;
  return f.read((uint8_t*)&p, sizeof(p)) == sizeof(p);
}

static void guardarConfig() {
  pp.putBool("act", S.activo);
  pp.putFloat("p0", S.p0);
  pp.putFloat("obj", S.objetivo);
  pp.putULong("ini", S.inicio);
  pp.putString("tanda", S.tanda);
  pp.putString("nom", S.nombre);
  pp.putBool("pau", S.pausado);
  pp.putULong("pauD", S.pausaDesde);
}

// Recalcula peso, merma y estimación (con mtx tomado).
static void recalcular() {
  const uint32_t ahora = ahoraEpoch();
  S.enVivo = !S.pausado && !isnan(ultPeso) && millis() - ultPesoMs < 5000;
  S.peso = S.enVivo ? ultPeso : NAN;
  uint32_t tPeso = ahora;                        // momento del peso que se muestra

  File f = LittleFS.open(ARCH, "r");
  const size_t n = f ? f.size() / sizeof(Punto) : 0;
  Punto ult;
  if (isnan(S.peso) && n && leerPunto(f, n - 1, ult)) { S.peso = ult.dg / 10.0f; tPeso = ult.t; }   // en pausa o sin balanza: el último registrado

  S.merma = (!isnan(S.peso) && S.p0 > 0) ? (S.p0 - S.peso) / S.p0 * 100.0f : NAN;
  S.dias  = (ahora && S.inicio && ahora > S.inicio) ? (ahora - S.inicio) / 86400.0f : 0;
  S.ritmo = NAN;
  S.falta = NAN;

  // Referencia para el ritmo: el punto de hace PESAJE_VENTANA_H horas (o el inicio, si es más nuevo).
  if (tPeso && !isnan(S.merma) && n) {
    const uint32_t buscado = tPeso - PESAJE_VENTANA_H * 3600UL;
    size_t lo = 0, hi = n - 1;                   // primer punto con t >= buscado
    Punto p;
    while (lo < hi) {
      const size_t m = (lo + hi) / 2;
      if (leerPunto(f, m, p) && p.t < buscado) lo = m + 1; else hi = m;
    }
    if (leerPunto(f, lo, p) && tPeso > p.t && tPeso - p.t >= PESAJE_MIN_H * 3600UL) {
      const float mRef = (S.p0 - p.dg / 10.0f) / S.p0 * 100.0f;
      S.ritmo = (S.merma - mRef) / ((tPeso - p.t) / 86400.0f);
    }
  }
  if (f) f.close();

  if (!isnan(S.merma) && S.objetivo > 0) {
    if (S.merma >= S.objetivo)            S.falta = 0;
    else if (!isnan(S.ritmo) && S.ritmo > 0.01f) S.falta = (S.objetivo - S.merma) / S.ritmo;
  }
}

void pesajeInit() {
  mtx = xSemaphoreCreateMutex();
  pp.begin("pesaje", false);
  S.activo   = pp.getBool("act", false);
  S.p0       = pp.getFloat("p0", 0);
  S.objetivo = pp.getFloat("obj", 30);
  S.inicio   = pp.getULong("ini", 0);
  S.pausado  = S.activo && pp.getBool("pau", false);
  S.pausaDesde = S.pausado ? pp.getULong("pauD", 0) : 0;
  strlcpy(S.tanda,  pp.getString("tanda", "").c_str(), sizeof(S.tanda));
  strlcpy(S.nombre, pp.getString("nom", "").c_str(),   sizeof(S.nombre));
  S.puntos = contarPuntos();
  Candado c;
  recalcular();
}

void pesajeLoop() {
  static uint32_t ultSeg = 0, ultCalc = 0;
  const uint32_t ms = millis();
  if (ms - ultSeg < 1000) return;
  ultSeg = ms;

  const BalanzaEstado b = balanzaEstado();
  bool valido = b.hayPeso && !(b.flags & BAL_SOBRECARGA);

  const uint32_t ahora = ahoraEpoch();
  bool cambio = false;
  {
    Candado c;
    // Casi sin peso con un seguimiento en curso: la pieza no está colgada. No se registra ni se
    // muestra como merma (sería ~100 %).
    S.descolgada = valido && S.activo && S.p0 > 0 && b.gramos < S.p0 * PESAJE_DESCOLGADA;
    if (S.descolgada) valido = false;
    if (valido) { ultPeso = b.gramos; ultPesoMs = ms; }
    if (S.pausado) { slotAct = 0; acum = 0; nAcum = 0; }   // en pausa no se junta nada
    else if (S.activo && ahora) {
      const uint32_t slot = ahora / PESAJE_INTERVALO_S;
      if (slotAct && slot != slotAct && nAcum) {              // terminó un intervalo: se guarda el promedio
        agregarPunto(slotAct * PESAJE_INTERVALO_S + PESAJE_INTERVALO_S / 2, (float)(acum / nAcum));
        cambio = true;
      }
      if (slot != slotAct) { slotAct = slot; acum = 0; nAcum = 0; }
      if (valido && (b.flags & BAL_ESTABLE)) { acum += b.gramos; nAcum++; }   // solo con el peso quieto
    }
    if (cambio || ms - ultCalc >= 5000) { ultCalc = ms; recalcular(); }
  }
}

PesajeEstado pesajeEstado() {
  Candado c;
  return S;
}

bool pesajeIniciar(float p0, float objetivo, uint32_t inicio, const char* tanda, const char* nombre) {
  if (!(p0 >= 1 && p0 <= 1100) || !(objetivo >= 1 && objetivo <= 80)) return false;
  const uint32_t ahora = ahoraEpoch();
  if (!inicio) inicio = ahora;
  if (!inicio) return false;
  if (ahora) {                                    // fecha de ingreso razonable: ni en el futuro ni de hace más de 2 años
    if (inicio > ahora) inicio = ahora;
    else if (ahora - inicio > 2UL * 365 * 86400) inicio = ahora - 2UL * 365 * 86400;
  }
  Candado c;
  LittleFS.remove(ARCH);
  S.puntos = 0;
  S.activo = true;
  S.p0 = p0;
  S.objetivo = objetivo;
  S.inicio = inicio;
  strlcpy(S.tanda,  tanda  ? tanda  : "", sizeof(S.tanda));
  strlcpy(S.nombre, nombre ? nombre : "", sizeof(S.nombre));
  S.pausado = false;
  S.pausaDesde = 0;
  agregarPunto(inicio, p0);                       // el punto de partida
  slotAct = 0; acum = 0; nAcum = 0;
  guardarConfig();
  recalcular();
  return true;
}

bool pesajeEditar(float p0, float objetivo, const char* tanda, const char* nombre) {
  if (!isnan(p0) && !(p0 >= 1 && p0 <= 1100)) return false;
  if (!isnan(objetivo) && !(objetivo >= 1 && objetivo <= 80)) return false;
  Candado c;
  if (!isnan(p0)) S.p0 = p0;
  if (!isnan(objetivo)) S.objetivo = objetivo;
  if (tanda)  strlcpy(S.tanda,  tanda,  sizeof(S.tanda));
  if (nombre) strlcpy(S.nombre, nombre, sizeof(S.nombre));
  guardarConfig();
  recalcular();
  return true;
}

void pesajeTerminar() {
  Candado c;
  S.activo = false;
  S.pausado = false;
  S.pausaDesde = 0;
  guardarConfig();
}

bool pesajePausar(bool pausar) {
  Candado c;
  if (!S.activo) return false;
  if (S.pausado == pausar) return true;
  S.pausado = pausar;
  S.pausaDesde = pausar ? ahoraEpoch() : 0;
  slotAct = 0; acum = 0; nAcum = 0;               // el intervalo a medias se descarta (pudo incluir el descuelgue)
  guardarConfig();
  recalcular();
  Serial.println(pausar ? "Seguimiento en pausa" : "Seguimiento retomado");
  return true;
}

#define JSON_PUNTOS 600
size_t pesajeJsonTam() { return JSON_PUNTOS * 26 + 160; }

void pesajeJson(Print& out) {
  Candado c;
  out.printf("{\"p0\":%.1f,\"obj\":%.1f,\"ini\":%lu,\"pts\":[", S.p0, S.objetivo, (unsigned long)S.inicio);
  File f = LittleFS.open(ARCH, "r");
  if (f) {
    const size_t n = f.size() / sizeof(Punto);
    const size_t paso = n > JSON_PUNTOS ? (n + JSON_PUNTOS - 1) / JSON_PUNTOS : 1;
    bool primero = true;
    Punto p;
    for (size_t i = 0; i < n; i += paso) {
      // el último punto siempre va (es el más nuevo)
      if (!leerPunto(f, i, p)) break;
      out.printf("%s[%lu,%.1f]", primero ? "" : ",", (unsigned long)p.t, p.dg / 10.0f);
      primero = false;
    }
    if (n && (n - 1) % paso && leerPunto(f, n - 1, p)) out.printf(",[%lu,%.1f]", (unsigned long)p.t, p.dg / 10.0f);
    f.close();
  }
  out.print("]}");
}
#endif
