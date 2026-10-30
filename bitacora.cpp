#include "bitacora.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

static const char* ARCH = BITACORA_ARCHIVO;
static const char* TMP  = "/bitacora.tmp";
static const char* BAK  = "/bitacora.bak";
static SemaphoreHandle_t mtx = nullptr;

void bitacoraInit() {
  mtx = xSemaphoreCreateMutex();
  // Si se cortó la luz justo entre los dos renombres de una confirmación, se recupera la copia.
  if (!LittleFS.exists(ARCH) && LittleFS.exists(BAK)) LittleFS.rename(BAK, ARCH);
}

size_t bitacoraTam() {
  if (!mtx) return 0;
  xSemaphoreTake(mtx, portMAX_DELAY);
  size_t n = 0;
  File f = LittleFS.open(ARCH, "r");
  if (f) { n = f.size(); f.close(); }
  xSemaphoreGive(mtx);
  return n;
}

static bool lineaValida(const String& l, bool& esBorrado) {
  JsonDocument d;
  esBorrado = false;
  if (deserializeJson(d, l) != DeserializationError::Ok) return false;
  if (!d.is<JsonObject>()) return false;
  const char* k = d["k"] | "";
  const size_t kl = strlen(k);
  esBorrado = d["x"].is<int>() && d["x"].as<int>() != 0;
  return kl > 0 && kl <= 24;
}

int bitacoraAgregar(const String& linea, bool usarTmp) {
  if (!mtx) return 400;
  if (linea.length() > BITACORA_MAX_LINEA) return 413;
  if (linea.indexOf('\n') >= 0 || linea.indexOf('\r') >= 0) return 400;
  bool borrado;
  if (!lineaValida(linea, borrado)) return 400;

  xSemaphoreTake(mtx, portMAX_DELAY);
  int rc = 0;
  const char* destino = usarTmp ? TMP : ARCH;
  size_t actual = 0;
  { File g = LittleFS.open(destino, "r"); if (g) { actual = g.size(); g.close(); } }
  const size_t libre = LittleFS.totalBytes() - LittleFS.usedBytes();
  // Los borrados pueden usar un margen extra: así siempre se puede liberar lugar aunque esté lleno.
  const size_t tope    = BITACORA_MAX_BYTES + (borrado ? BITACORA_MARGEN_BORRAR : 0);
  const size_t reserva = borrado ? BITACORA_RESERVA / 2 : BITACORA_RESERVA;
  if (actual + linea.length() + 1 > tope) rc = 507;
  else if (libre < reserva + linea.length() + 1) rc = 507;
  else {
    File f = LittleFS.open(destino, "a");
    if (!f) rc = 507;
    else {
      const size_t w = f.print(linea) + f.print('\n');
      f.close();
      if (w != linea.length() + 1) rc = 507;
    }
  }
  xSemaphoreGive(mtx);
  return rc;
}

bool bitacoraTmpReiniciar() {
  if (!mtx) return false;
  xSemaphoreTake(mtx, portMAX_DELAY);
  LittleFS.remove(TMP);
  File f = LittleFS.open(TMP, "w");
  const bool ok = (bool)f;
  if (f) f.close();
  xSemaphoreGive(mtx);
  return ok;
}

bool bitacoraTmpConfirmar() {
  if (!mtx) return false;
  xSemaphoreTake(mtx, portMAX_DELAY);
  bool ok = false;
  if (LittleFS.exists(TMP)) {
    LittleFS.remove(BAK);
    if (LittleFS.exists(ARCH)) LittleFS.rename(ARCH, BAK);
    ok = LittleFS.rename(TMP, ARCH);
    if (!ok && LittleFS.exists(BAK)) LittleFS.rename(BAK, ARCH);   // deshacer
  }
  xSemaphoreGive(mtx);
  return ok;
}

void bitacoraBorrar() {
  if (!mtx) return;
  xSemaphoreTake(mtx, portMAX_DELAY);
  LittleFS.remove(ARCH); LittleFS.remove(TMP); LittleFS.remove(BAK);
  xSemaphoreGive(mtx);
}
