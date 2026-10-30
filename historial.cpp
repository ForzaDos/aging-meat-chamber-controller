#include "historial.h"
#include "config.h"
#include "archivo.h"
#include <LittleFS.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#define MAGIC    0xC4A7C001UL
#define NIVELES  HIST_NIVELES
#define NIVELES_FLASH 4          // el último nivel (1 hora en vivo) no se guarda en flash

// Registro de 10 bytes. Las temperaturas y humedades se guardan x10 (una decimal).
struct __attribute__((packed)) Registro {
  uint32_t ts;       // epoch UTC del inicio del intervalo
  int16_t  temp10;   // °C x 10
  int16_t  hum10;    // %HR x 10
  uint8_t  flags;    // bit0 hum, bit1 deshum, bit2 frío, bit3 ventilador, bit4 otros, bit5 luz (OR dentro del intervalo)
  uint8_t  pad;
};

struct __attribute__((packed)) Cabecera {
  uint32_t magic;
  uint16_t head;     // próxima posición de escritura
  uint16_t count;    // registros válidos
};

static const char*    ARCHIVO[NIVELES] = { "/h_1d.bin", "/h_1w.bin", "/h_1m.bin", "/h_3m.bin", nullptr };
static const uint16_t PUNTOS [NIVELES] = { 1440, 2016, 1440, 1440, 360 };
static const uint32_t DT     [NIVELES] = { 60, 300, 1800, 5400, 10 };   // segundos por punto

struct Nivel {
  Registro* buf   = nullptr;
  uint16_t  head  = 0;
  uint16_t  count = 0;
  uint32_t  slot  = 0;      // intervalo en curso (epoch / DT)
  double    sumT  = 0;
  double    sumH  = 0;
  uint32_t  n     = 0;      // muestras acumuladas en el intervalo en curso
  uint8_t   flags = 0;
  bool      sucio = false;  // hay datos nuevos sin volcar a flash
};

static Nivel niv[NIVELES];
static SemaphoreHandle_t mtx = nullptr;
static uint32_t ultimoVolcado = 0;

// ---------------------------------------------------------------------
static void cargar(int i) {
  if (!ARCHIVO[i]) return;
  archivoRecuperar(ARCHIVO[i]);
  File f = LittleFS.open(ARCHIVO[i], "r");
  if (!f) return;

  const size_t esperado = sizeof(Cabecera) + (size_t)PUNTOS[i] * sizeof(Registro);
  Cabecera c;
  if (f.size() != esperado ||
      f.read((uint8_t*)&c, sizeof(c)) != sizeof(c) ||
      c.magic != MAGIC || c.head >= PUNTOS[i] || c.count > PUNTOS[i]) {
    f.close();
    Serial.printf("Historial nivel %d: archivo inválido, se empieza vacío\n", i);
    return;
  }
  f.read((uint8_t*)niv[i].buf, (size_t)PUNTOS[i] * sizeof(Registro));
  niv[i].head  = c.head;
  niv[i].count = c.count;
  f.close();
}

static bool guardar(int i) {
  const Cabecera c = { MAGIC, niv[i].head, niv[i].count };
  return archivoGuardarSeguro(ARCHIVO[i], &c, sizeof(c), niv[i].buf, (size_t)PUNTOS[i] * sizeof(Registro));
}

// Cierra el intervalo en curso del nivel i: guarda el promedio y reinicia acumuladores.
// (Se llama con el mutex tomado.)
static void emitir(int i) {
  Nivel& L = niv[i];
  if (L.n == 0) return;

  Registro r;
  r.ts     = L.slot * DT[i];
  r.temp10 = (int16_t)lroundf((float)(L.sumT / L.n) * 10.0f);
  r.hum10  = (int16_t)lroundf((float)(L.sumH / L.n) * 10.0f);
  r.flags  = L.flags;
  r.pad    = 0;

  L.buf[L.head] = r;
  L.head = (L.head + 1) % PUNTOS[i];
  if (L.count < PUNTOS[i]) L.count++;

  L.sumT = L.sumH = 0;
  L.n = 0;
  L.flags = 0;
  L.sucio = true;
}

// ---------------------------------------------------------------------
void historialInit() {
  mtx = xSemaphoreCreateMutex();
  for (int i = 0; i < NIVELES; i++) {
    niv[i].buf = (Registro*)calloc(PUNTOS[i], sizeof(Registro));
    if (!niv[i].buf) {
      Serial.printf("Historial nivel %d: sin RAM\n", i);
      continue;
    }
    cargar(i);
  }
  ultimoVolcado = millis();
}

void historialMuestra(float t, float h, uint8_t flags) {
  const time_t ahora = time(nullptr);
  if (ahora < 1700000000) return;   // todavía sin hora NTP: no registrar con fechas falsas

  xSemaphoreTake(mtx, portMAX_DELAY);
  for (int i = 0; i < NIVELES; i++) {
    Nivel& L = niv[i];
    if (!L.buf) continue;
    const uint32_t slot = (uint32_t)ahora / DT[i];
    if (L.n > 0 && slot != L.slot) emitir(i);
    L.slot = slot;
    L.sumT += t;
    L.sumH += h;
    L.n++;
    L.flags |= flags;
  }
  xSemaphoreGive(mtx);
}

void historialLoop() {
  if (millis() - ultimoVolcado < (uint32_t)VOLCADO_MIN * 60000UL) return;
  historialVolcar();
}

void historialVolcar() {
  ultimoVolcado = millis();
  for (int i = 0; i < NIVELES_FLASH; i++) {
    if (!niv[i].buf || !niv[i].sucio) continue;
    xSemaphoreTake(mtx, portMAX_DELAY);
    if (guardar(i)) niv[i].sucio = false;
    else Serial.printf("Historial nivel %d: error al volcar a flash\n", i);
    xSemaphoreGive(mtx);
  }
}

uint16_t historialCantidad(uint8_t nivel) {
  if (nivel >= NIVELES || !niv[nivel].buf) return 0;
  return niv[nivel].count;
}

// Formato: {"dt":60,"g":4,"t0":<epoch>,"k":[0,4,8,...],"t":[...],"h":[...],"f":[...]}
// Para que la respuesta sea chica (y no se agote la memoria del ESP32 cuando el historial
// está lleno), si hay más de MAX_PUNTOS_JSON registros se agrupan de a "g" y se envía
// el promedio de cada grupo. Cada punto i está en el instante t0 + k[i]*dt; los saltos
// grandes en k son huecos (equipo apagado) y la página los dibuja como cortes en la línea.
#define MAX_PUNTOS_JSON 400

// Tamaño máximo de la respuesta, para reservar el búfer de una sola vez (si crece de a pedazos
// fragmenta la memoria). Por punto: k (5) + t (6) + h (5) + f (2) + 4 comas.
size_t historialJsonTam(uint8_t nivel) {
  if (nivel >= NIVELES) nivel = 0;
  const uint16_t cnt = niv[nivel].buf ? niv[nivel].count : 0;
  const uint16_t g = (cnt > MAX_PUNTOS_JSON) ? (uint16_t)((cnt + MAX_PUNTOS_JSON - 1) / MAX_PUNTOS_JSON) : 1;
  return (size_t)((cnt + g - 1) / g) * 28 + 96;   // 28 B por punto: [k,t,h] con k de hasta 7 dígitos tras apagones largos
}

void historialJson(uint8_t nivel, Print& out) {
  if (nivel >= NIVELES) nivel = 0;
  Nivel& L = niv[nivel];
  const uint16_t N = PUNTOS[nivel];

  xSemaphoreTake(mtx, portMAX_DELAY);
  const uint16_t cnt = L.buf ? L.count : 0;
  const uint16_t ini = (uint16_t)((L.head + N - cnt) % N);   // posición del registro más viejo
  const uint32_t t0  = cnt ? L.buf[ini].ts : 0;
  const uint16_t g   = (cnt > MAX_PUNTOS_JSON) ? (uint16_t)((cnt + MAX_PUNTOS_JSON - 1) / MAX_PUNTOS_JSON) : 1;
  const uint16_t m   = (uint16_t)((cnt + g - 1) / g);        // puntos que se envían

  out.printf("{\"dt\":%lu,\"g\":%u,\"t0\":%lu,\"k\":[", (unsigned long)DT[nivel], (unsigned)g, (unsigned long)t0);
  for (uint16_t j = 0; j < m; j++) {
    if (j) out.print(',');
    out.print((unsigned long)((L.buf[(ini + (uint32_t)j * g) % N].ts - t0) / DT[nivel]));
  }

  // Promedio de cada grupo de "g" registros (temperatura o humedad, x10 en memoria).
  auto promedios = [&](const char* clave, bool temp) {
    out.print(clave);
    for (uint16_t j = 0; j < m; j++) {
      float suma = 0; uint16_t c = 0;
      for (uint32_t i = (uint32_t)j * g; i < cnt && i < (uint32_t)(j + 1) * g; i++, c++) {
        const Registro& r = L.buf[(ini + i) % N];
        suma += temp ? r.temp10 : r.hum10;
      }
      if (j) out.print(',');
      out.print(c ? suma / c / 10.0f : 0.0f, 1);
    }
  };
  promedios("],\"t\":[", true);
  promedios("],\"h\":[", false);

  out.print("],\"f\":[");
  for (uint16_t j = 0; j < m; j++) {
    uint8_t fl = 0;
    for (uint32_t i = (uint32_t)j * g; i < cnt && i < (uint32_t)(j + 1) * g; i++) fl |= L.buf[(ini + i) % N].flags;
    if (j) out.print(',');
    out.print((unsigned int)fl);
  }
  out.print("]}");
  xSemaphoreGive(mtx);
}

HistResumen historialResumen(uint32_t desde, uint32_t hasta) {
  HistResumen r = {};
  const uint32_t ahora = (uint32_t)time(nullptr);
  if (ahora < 1700000000 || hasta <= desde) return r;

  // Primer nivel cuya memoria llega hasta "desde".
  int nivel = NIVELES_FLASH - 1;
  for (int i = 0; i < NIVELES_FLASH; i++) {
    if ((uint64_t)PUNTOS[i] * DT[i] >= (uint64_t)(ahora > desde ? ahora - desde : 0)) { nivel = i; break; }
  }
  Nivel& L = niv[nivel];
  const uint16_t N = PUNTOS[nivel];
  double sT = 0, sH = 0; uint32_t nh = 0, nd = 0, nf = 0;

  xSemaphoreTake(mtx, portMAX_DELAY);
  const uint16_t cnt = L.buf ? L.count : 0;
  const uint16_t ini = (uint16_t)((L.head + N - cnt) % N);
  for (uint16_t i = 0; i < cnt; i++) {
    const Registro& g = L.buf[(ini + i) % N];
    if (g.ts < desde || g.ts >= hasta) continue;
    const float t = g.temp10 / 10.0f, hu = g.hum10 / 10.0f;
    if (r.n == 0) { r.tMin = r.tMax = t; r.hMin = r.hMax = hu; }
    if (t < r.tMin) r.tMin = t;   if (t > r.tMax) r.tMax = t;
    if (hu < r.hMin) r.hMin = hu; if (hu > r.hMax) r.hMax = hu;
    sT += t; sH += hu; r.n++;
    if (g.flags & 1) nh++;
    if (g.flags & 2) nd++;
    if (g.flags & 4) nf++;
  }
  xSemaphoreGive(mtx);

  if (r.n) {
    r.tProm = (float)(sT / r.n);  r.hProm = (float)(sH / r.n);
    r.pHum = 100.0f * nh / r.n;   r.pDesh = 100.0f * nd / r.n;   r.pFrio = 100.0f * nf / r.n;
  }
  return r;
}

void historialBorrar() {
  xSemaphoreTake(mtx, portMAX_DELAY);
  for (int i = 0; i < NIVELES; i++) {
    Nivel& L = niv[i];
    if (L.buf) memset(L.buf, 0, (size_t)PUNTOS[i] * sizeof(Registro));
    L.head = 0; L.count = 0; L.sumT = L.sumH = 0; L.n = 0; L.flags = 0; L.sucio = false;
    if (ARCHIVO[i]) LittleFS.remove(ARCHIVO[i]);
  }
  xSemaphoreGive(mtx);
}
