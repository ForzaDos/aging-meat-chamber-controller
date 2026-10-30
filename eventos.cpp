#include "eventos.h"
#include "config.h"
#include "archivo.h"
#include <LittleFS.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#define EV_MAX    1000
#define EV_MAGIC  0xE7E70001UL
#define SIN_HORA  0x80000000UL    // ts con este bit = segundos desde el arranque (todavía sin hora de red)

static const char* ARCHIVO = "/eventos.bin";

struct __attribute__((packed)) Evento   { uint32_t ts; uint8_t cod; uint8_t on; };
struct __attribute__((packed)) Cabecera { uint32_t magic; uint16_t head; uint16_t count; };

static Evento*  buf = nullptr;
static uint16_t head = 0, count = 0;
static bool     sucio = false, pendientes = false;
static uint32_t ultVolcado = 0;
static SemaphoreHandle_t mtx = nullptr;

static uint32_t marcaDeTiempo() {
  const time_t t = time(nullptr);
  if (t > 1700000000) return (uint32_t)t;
  pendientes = true;
  return SIN_HORA | (millis() / 1000);
}

static void cargar() {
  archivoRecuperar(ARCHIVO);
  File f = LittleFS.open(ARCHIVO, "r");
  if (!f) return;
  Cabecera c;
  const size_t datos = (size_t)EV_MAX * sizeof(Evento);
  if (f.size() != sizeof(c) + datos || f.read((uint8_t*)&c, sizeof(c)) != sizeof(c) ||
      c.magic != EV_MAGIC || c.head >= EV_MAX || c.count > EV_MAX) {
    f.close();
    return;
  }
  f.read((uint8_t*)buf, datos);
  f.close();
  head = c.head;
  count = c.count;
  // Eventos de un arranque anterior que nunca recibieron hora: quedan como "hora desconocida".
  for (uint16_t i = 0; i < EV_MAX; i++) if (buf[i].ts & SIN_HORA) buf[i].ts = 0;
}

static bool guardar() {
  const Cabecera c = { EV_MAGIC, head, count };
  return archivoGuardarSeguro(ARCHIVO, &c, sizeof(c), buf, (size_t)EV_MAX * sizeof(Evento));
}

void eventosInit() {
  mtx = xSemaphoreCreateMutex();
  buf = (Evento*)calloc(EV_MAX, sizeof(Evento));
  if (!buf) { Serial.println("Eventos: sin RAM"); return; }
  cargar();
  ultVolcado = millis();
  eventosRegistrar(EV_ARRANQUE, true);
}

void eventosRegistrar(uint8_t cod, bool on, bool manual) {
  if (!buf) return;
  xSemaphoreTake(mtx, portMAX_DELAY);
  buf[head].ts  = marcaDeTiempo();
  buf[head].cod = cod;
  buf[head].on  = (on ? 1 : 0) | (manual ? 2 : 0);   // bit 0 = encendido, bit 1 = forzado a mano
  head = (head + 1) % EV_MAX;
  if (count < EV_MAX) count++;
  sucio = true;
  xSemaphoreGive(mtx);
}

void eventosLoop() {
  if (!buf) return;

  // Cuando llega la hora de internet, se la pone a los eventos que se anotaron sin hora.
  if (pendientes) {
    const time_t t = time(nullptr);
    if (t > 1700000000) {
      const uint32_t arranque = (uint32_t)t - millis() / 1000;
      xSemaphoreTake(mtx, portMAX_DELAY);
      for (uint16_t i = 0; i < EV_MAX; i++) {
        if (buf[i].ts & SIN_HORA) buf[i].ts = arranque + (buf[i].ts & ~SIN_HORA);
      }
      pendientes = false;
      sucio = true;
      xSemaphoreGive(mtx);
    }
  }

  if (sucio && millis() - ultVolcado > (uint32_t)VOLCADO_MIN * 60000UL) eventosVolcar();
}

void eventosVolcar() {
  if (!buf || !sucio) return;
  ultVolcado = millis();
  xSemaphoreTake(mtx, portMAX_DELAY);
  if (guardar()) sucio = false;
  xSemaphoreGive(mtx);
}

// {"total":N,"ev":[[ts,cod,on,manual],...]}  del más nuevo al más viejo. ts = 0 si no tiene hora.
void eventosJson(Print& out, uint16_t n, uint16_t saltar) {
  if (!buf) { out.print("{\"total\":0,\"ev\":[]}"); return; }
  xSemaphoreTake(mtx, portMAX_DELAY);
  out.printf("{\"total\":%u,\"ev\":[", (unsigned)count);
  uint16_t k = 0;
  for (uint32_t j = saltar; j < count && k < n; j++, k++) {
    const Evento& e = buf[(head + EV_MAX - 1 - j) % EV_MAX];
    const uint32_t ts = (e.ts & SIN_HORA) ? 0 : e.ts;
    if (k) out.print(',');
    out.printf("[%lu,%u,%u,%u]", (unsigned long)ts, (unsigned)e.cod, (unsigned)(e.on & 1), (unsigned)((e.on >> 1) & 1));
  }
  out.print("]}");
  xSemaphoreGive(mtx);
}

void eventosBorrar() {
  if (!buf) return;
  xSemaphoreTake(mtx, portMAX_DELAY);
  memset(buf, 0, (size_t)EV_MAX * sizeof(Evento));
  head = 0; count = 0; sucio = false; pendientes = false;
  LittleFS.remove(ARCHIVO);
  xSemaphoreGive(mtx);
}
