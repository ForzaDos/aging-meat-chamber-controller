#include "web.h"
#include "config.h"
#include "hardware.h"
#include "control.h"
#include "historial.h"
#include "calibracion.h"
#include "eventos.h"
#include "bitacora.h"
#include "redwifi.h"
#include "balanza.h"
#include "pesaje.h"
#include "pagina.h"
#include "pagina_bitacora.h"
#include "pagina_wifi.h"
#include "chartjs_gz.h"
#include <algorithm>
#include <vector>
#include <WiFi.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include <WebAuthentication.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <time.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_ota_ops.h>
#include <Update.h>

static AsyncWebServer server(80);

// ---------- Acceso con clave (HTTP Digest: la clave no viaja en texto plano al entrar) ----------
// En NVS ("web") se guarda el hash de la clave vigente y el de la WEB_CLAVE de config.h con que
// se generó: si config.h trae otra clave, manda esa (así se recupera una clave olvidada).
#define WEB_REINO  "Camara de curado"
static_assert(sizeof(WEB_CLAVE) > 8, "WEB_CLAVE tiene que tener al menos 8 caracteres");
static AsyncAuthenticationMiddleware acceso;
static String claveHash;   // solo lo usan los pedidos web (tarea async_tcp)

static void accesoInit() {
  const String base = generateDigestHash(WEB_USUARIO, WEB_CLAVE, WEB_REINO);
  Preferences p;
  p.begin("web", false);
  if (p.getString("base", "") != base) { p.putString("base", base); p.putString("hash", base); }
  claveHash = p.getString("hash", base);
  p.end();
  acceso.setUsername(WEB_USUARIO);
  acceso.setRealm(WEB_REINO);
  acceso.setAuthType(AsyncAuthType::AUTH_DIGEST);
  acceso.setPasswordHash(claveHash.c_str());
  acceso.setAuthFailureMessage("Clave incorrecta");
}

// Protección CSRF: toda escritura tiene que traer el encabezado X-Camara. Un formulario de otra
// página no lo puede poner, y un fetch de otra página con ese encabezado necesita un permiso CORS
// que el equipo nunca da.
static void exigirEncabezado(AsyncWebServerRequest* r, ArMiddlewareNext next) {
  if (r->method() == HTTP_POST && !r->hasHeader("X-Camara")) {
    r->send(403, "application/json", "{\"ok\":false}");
    return;
  }
  next();
}

// POST /api/clave  actual=, nueva=   (8 a 32 caracteres ASCII visibles, sin espacios).
// Después el navegador vuelve a pedir usuario y clave.
static void cambiarClave(AsyncWebServerRequest* r) {
  if (WEB_SIN_CLAVE) { r->send(403, "application/json", "{\"ok\":false,\"sinClave\":true}"); return; }
  if (!r->hasParam("actual", true) || !r->hasParam("nueva", true)) { r->send(400, "application/json", "{\"ok\":false}"); return; }
  const String actual = r->getParam("actual", true)->value();
  const String nueva  = r->getParam("nueva", true)->value();
  if (generateDigestHash(WEB_USUARIO, actual.c_str(), WEB_REINO) != claveHash) {
    r->send(403, "application/json", "{\"ok\":false,\"error\":\"actual\"}");
    return;
  }
  bool ok = nueva.length() >= 8 && nueva.length() <= 32;
  for (size_t i = 0; ok && i < nueva.length(); i++) ok = nueva[i] > ' ' && nueva[i] <= '~';
  if (!ok) { r->send(400, "application/json", "{\"ok\":false,\"error\":\"nueva\"}"); return; }
  claveHash = generateDigestHash(WEB_USUARIO, nueva.c_str(), WEB_REINO);
  Preferences p;
  p.begin("web", false);
  p.putString("hash", claveHash);
  p.end();
  acceso.setPasswordHash(claveHash.c_str());
  Serial.println("Clave de la web cambiada");
  r->send(200, "application/json", "{\"ok\":true}");
}

// Sobrevive a un reinicio por software (no a un corte de luz): permite saber que el reinicio fue por memoria baja.
static RTC_NOINIT_ATTR uint32_t marcaReinicioMem;
#define MARCA_MEM 0x4D454D31u
void webMarcarReinicioPorMemoria() { marcaReinicioMem = MARCA_MEM; }

static const char* motivoReinicio() {
  static bool leido = false;      // la marca se consume una sola vez: sobrevive a cualquier reinicio por software
  static bool pormem = false;
  if (!leido) {
    leido = true;
    pormem = (esp_reset_reason() == ESP_RST_SW && marcaReinicioMem == MARCA_MEM);
    marcaReinicioMem = 0;
  }
  if (pormem) return "reinicio automatico por memoria baja";
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return "encendido normal";
    case ESP_RST_EXT:       return "boton de reinicio";
    case ESP_RST_SW:        return "reinicio por software";
    case ESP_RST_PANIC:     return "error del programa (panic)";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:       return "watchdog (programa trabado)";
    case ESP_RST_BROWNOUT:  return "caida de tension (brownout)";
    default:                return "otro";
  }
}

// El JSON se escribe directo en un búfer del tamaño justo (una sola reserva de memoria, sin copias).
static void enviarJson(AsyncWebServerRequest* r, JsonDocument& d) {
  AsyncResponseStream* res = r->beginResponseStream("application/json", measureJson(d) + 1);
  res->addHeader("Cache-Control", "no-store");
  serializeJson(d, *res);
  r->send(res);
}

static uint32_t segEncendido() { return (uint32_t)(esp_timer_get_time() / 1000000); }   // millis() vuelve a 0 a los 49 días

// Estado del test de relés: activo, relé encendido (0..5, -1 = pausa), número 1..6 y segundos que faltan.
static void agregarTest(JsonDocument& d) {
  JsonObject t = d["test"].to<JsonObject>();
  t["activo"] = controlTestActivo();
  t["modo"]   = controlTestModo();      // 1 de a uno, 2 sumar 1 a 6, 3 todos juntos
  t["mascara"]= controlTestMascara();   // bit i = función i encendida
  t["avanzado"] = (bool)TEST_AVANZADO;
  t["largoS"] = TEST_LARGO_MS / 1000;
  t["rele"]   = controlTestRele();
  t["num"]    = controlTestNumero();
  t["hechos"] = controlTestHechos();
  t["seg"]    = controlTestSegRestantes();
  t["onMs"]   = TEST_ON_MS;
  JsonArray o = t["orden"].to<JsonArray>();      // función conectada a cada relé, de IN1 a IN6
  for (uint8_t k = 0; k < TEST_RELES; k++) o.add(controlTestOrden(k));
}

// Estado del asistente de calibración con sal.
static void agregarCalib(JsonDocument& d) {
  const CalibEstado c = calibEstado();
  JsonObject o = d["calib"].to<JsonObject>();
  o["activo"] = c.activo;
  if (!c.activo) return;
  auto r1 = [](float v) { return roundf(v * 10.0f) / 10.0f; };
  o["ref"]     = r1(c.ref);
  if (!isnan(c.actual)) o["actual"] = r1(c.actual);
  if (!isnan(c.prom))  { o["prom"] = roundf(c.prom * 100.0f) / 100.0f; o["rango"] = roundf(c.rango * 100.0f) / 100.0f; }
  o["min"]       = c.minutos;
  o["minMin"]    = CAL_MIN_MIN;
  o["estable"]   = c.estable;
  o["sePuede"]   = c.sePuede;
  o["propuesto"] = c.propuesto;
}

// POST /api/calib  accion=iniciar  |  accion=cancelar  |  accion=aplicar   (calibración con sal común)
static void ordenCalib(AsyncWebServerRequest* r) {
  const String a = r->hasParam("accion", true) ? r->getParam("accion", true)->value() : "";
  ControlCandado c;
  if (a == "iniciar") {
    calibIniciar();
  } else if (a == "cancelar") {
    calibCancelar();
  } else if (a == "aplicar") {
    if (!calibAplicar()) { r->send(409, "application/json", "{\"ok\":false}"); return; }
  } else { r->send(400, "application/json", "{\"ok\":false}"); return; }
  r->send(200, "application/json", "{\"ok\":true}");
}

// GET  /api/test                      -> estado del test
// POST /api/test  accion=iniciar&confirmar=SIN220[&modo=uno|suma|todos]   |   accion=detener
static void enviarTest(AsyncWebServerRequest* r) {
  JsonDocument d;
  agregarTest(d);
  enviarJson(r, d);
}
static void ordenTest(AsyncWebServerRequest* r) {
  const String a = r->hasParam("accion", true) ? r->getParam("accion", true)->value() : "";
  if (a == "detener") { controlTestPedirModo(TEST_NINGUNO); r->send(200, "application/json", "{\"ok\":true}"); return; }
  if (a == "iniciar" && controlParadaActiva()) { r->send(409, "application/json", "{\"ok\":false,\"motivo\":\"parada\"}"); return; }
  if (a == "iniciar" && r->hasParam("confirmar", true) && r->getParam("confirmar", true)->value() == "SIN220") {
    const String m = r->hasParam("modo", true) ? r->getParam("modo", true)->value() : "uno";
    const uint8_t modo = m == "suma" ? TEST_SUMA : m == "todos" ? TEST_TODOS : TEST_UNO;
    uint32_t espera;
    { ControlCandado c; espera = controlTestPedirModo(modo); }   // sin TEST_AVANZADO solo acepta TEST_UNO
    if (espera) {
      char b[64];
      snprintf(b, sizeof(b), "{\"ok\":false,\"espera\":%lu}", (unsigned long)espera);
      r->send(409, "application/json", b);
    } else {
      r->send(200, "application/json", "{\"ok\":true}");
    }
    return;
  }
  r->send(400, "application/json", "{\"ok\":false}");
}

static void enviarEstado(AsyncWebServerRequest* r) {
  JsonDocument d;
  ControlCandado c;                         // que no cambie nada mientras se copia

  if (est.sensorOk) {                       // sin lectura válida se omiten temp/hum
    d["temp"] = roundf(est.temp * 10.0f) / 10.0f;
    d["hum"]  = roundf(est.hum  * 10.0f) / 10.0f;
  }
  d["sensorOk"]    = est.sensorOk;
  d["alarmaTemp"]  = est.alarmaTemp;
  d["releHum"]     = est.releHum;
  d["releDesh"]    = est.releDesh;
  d["releFrio"]    = est.releFrio;
  d["releVent"]    = est.releVent;
  d["releLuz"]     = est.releLuz;
  d["releOtros"]   = est.releOtros;
  d["ventSeg"]     = est.ventSeg;
  agregarTest(d);

  // Forzado manual de humidificador, deshumidificador y refrigeración:
  // modo 0 = automático, 1 = encendido a mano, 2 = apagado a mano; seg = lo que falta para volver a automático.
  JsonArray man = d["manual"].to<JsonArray>();
  JsonArray manSeg = d["manualSeg"].to<JsonArray>();
  for (uint8_t i = 0; i < 3; i++) { man.add(est.manual[i]); manSeg.add(est.manualSeg[i]); }

  // Pin y número de relé (1..6) de cada salida (hum, desh, frío, vent, AUX1, AUX2), para la página.
  JsonArray pines = d["pines"].to<JsonArray>();
  JsonArray nrel  = d["nrel"].to<JsonArray>();
  const uint8_t p6[6] = { PIN_RELE_HUM, PIN_RELE_DESH, PIN_RELE_FRIO, PIN_RELE_VENT, PIN_RELE_LUZ, PIN_RELE_OTROS };
  for (uint8_t p : p6) { pines.add(p); nrel.add(numeroRele(p)); }

  d["activo"]          = cfg.activo;
  d["parada"]          = controlParadaActiva();
  d["humSP"]           = cfg.humSP;
  d["humHist"]         = cfg.humHist;
  d["tempSP"]          = cfg.tempSP;
  d["tempHist"]        = cfg.tempHist;
  d["tempBanda"]       = cfg.tempBanda;
  d["minOnS"]          = cfg.minOnS;
  d["minOnTempS"]      = cfg.minOnTempS;
  d["minOffS"]         = cfg.minOffS;
  d["manualMin"]       = cfg.manualMin;
  d["ventActivo"]      = cfg.ventActivo;
  d["ventPeriodoH"]    = cfg.ventPeriodoH;
  d["ventDuracionMin"] = cfg.ventDuracionMin;
  d["ventAnclaMin"]    = cfg.ventAnclaMin;
  d["offT"]            = cfg.offT;
  d["offH"]            = cfg.offH;
  if (!isnan(est.tempCruda)) { d["tCruda"] = roundf(est.tempCruda * 10.0f) / 10.0f; d["hCruda"] = roundf(est.humCruda * 10.0f) / 10.0f; }
  agregarCalib(d);
  d["nomAux1"]         = cfg.nomAux1;
  d["nomAux2"]         = cfg.nomAux2;

  d["hora"]   = (uint32_t)time(nullptr);
  d["rssi"]   = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;
  enviarJson(r, d);
}

// Guarda lo que venga en el POST (puede ser solo una parte: clima o ventilador).
static void guardarConfig(AsyncWebServerRequest* r) {
  auto tiene = [&](const char* k) { return r->hasParam(k, true); };
  auto texto = [&](const char* k) { return r->getParam(k, true)->value(); };
  auto leer  = [&](const char* k, float lo, float hi, float& dst) {
    if (!tiene(k)) return;
    const float v = texto(k).toFloat();
    if (isfinite(v)) dst = constrain(v, lo, hi);   // "nan" o "inf" se ignoran
  };
  ControlCandado c;

  leer("humSP",     HUM_SP_MIN,  HUM_SP_MAX,  cfg.humSP);
  leer("humHist",   0.5f, 10.0f, cfg.humHist);
  leer("tempSP",    TEMP_SP_MIN, TEMP_SP_MAX, cfg.tempSP);
  leer("tempHist",  0.5f,  5.0f, cfg.tempHist);
  leer("tempBanda", 1.0f, 10.0f, cfg.tempBanda);

  auto leerEntero = [&](const char* k, float lo, float hi, uint16_t& dst) {
    float v = dst; leer(k, lo, hi, v); dst = (uint16_t)v;
  };
  leerEntero("minOnS",          0, 600, cfg.minOnS);
  leerEntero("minOnTempS",      0, 600, cfg.minOnTempS);
  leerEntero("minOffS",         0, 600, cfg.minOffS);
  float m = cfg.manualMin; leer("manualMin", MANUAL_MIN_MIN, MANUAL_MIN_MAX, m);
  cfg.manualMin = (uint16_t)(m / 5.0f + 0.5f) * 5;                     // de a 5 minutos
  leerEntero("ventDuracionMin", 0, VENT_DURACION_MAX, cfg.ventDuracionMin);
  leerEntero("ventAnclaMin",    0, 1439, cfg.ventAnclaMin);
  if (tiene("ventPeriodoH")) cfg.ventPeriodoH = controlPeriodoValido(texto("ventPeriodoH").toInt());

  if (tiene("ventActivo")) cfg.ventActivo = (texto("ventActivo") == "1");
  leer("offT", -OFF_T_MAX, OFF_T_MAX, cfg.offT);
  leer("offH", -OFF_H_MAX, OFF_H_MAX, cfg.offH);
  if (tiene("nomAux1")) controlNombreAux(0, texto("nomAux1").c_str());
  if (tiene("nomAux2")) controlNombreAux(1, texto("nomAux2").c_str());
  if (tiene("activo")) {
    const bool antes = cfg.activo;
    cfg.activo = (texto("activo") == "1");
    if (cfg.activo != antes) eventosRegistrar(EV_PAUSA, !cfg.activo);
  }

  controlGuardarConfig();
  r->send(200, "application/json", "{\"ok\":true}");
}

// Relés manuales.
//   cual=luz|otros            estado=1|0   (luz = AUX1, otros = AUX2)
//   cual=hum|desh|frio        modo=auto|on|off     (forzado temporal, vuelve solo a automático)
// Si el encendido todavía no se permite (tiempo mínimo apagado) responde 409 con "espera" en segundos.
static void cambiarManual(AsyncWebServerRequest* r) {
  if (!r->hasParam("cual", true)) { r->send(400, "text/plain", "faltan datos"); return; }
  ControlCandado c;
  if (controlTestActivo()) { r->send(423, "application/json", "{\"ok\":false,\"test\":true}"); return; }
  const String cual = r->getParam("cual", true)->value();

  int rele = -1;
  if      (cual == "hum")  rele = R_HUM;
  else if (cual == "desh") rele = R_DESH;
  else if (cual == "frio") rele = R_FRIO;

  if (rele >= 0) {
    if (!r->hasParam("modo", true)) { r->send(400, "text/plain", "falta el modo"); return; }
    const String m = r->getParam("modo", true)->value();
    uint8_t modo;
    if      (m == "auto") modo = MAN_AUTO;
    else if (m == "on")   modo = MAN_ON;
    else if (m == "off")  modo = MAN_OFF;
    else { r->send(400, "text/plain", "modo desconocido"); return; }
    const uint32_t espera = controlForzar((uint8_t)rele, modo);
    if (espera == FORZAR_RECHAZADO) {
      char b[64];
      snprintf(b, sizeof(b), "{\"ok\":false,\"motivo\":\"%s\"}", controlForzarMotivo());
      r->send(409, "application/json", b);
    } else if (espera) {
      char b[64];
      snprintf(b, sizeof(b), "{\"ok\":false,\"espera\":%lu}", (unsigned long)espera);
      r->send(409, "application/json", b);
    } else {
      r->send(200, "application/json", "{\"ok\":true}");
    }
    return;
  }

  if (!r->hasParam("estado", true)) { r->send(400, "text/plain", "faltan datos"); return; }
  const bool on = (r->getParam("estado", true)->value() == "1");
  if (on && controlParadaActiva() && (cual == "luz" || cual == "otros")) { r->send(409, "application/json", "{\"ok\":false,\"motivo\":\"parada\"}"); return; }
  if      (cual == "luz")   controlSetLuz(on);
  else if (cual == "otros") controlSetOtros(on);
  else { r->send(400, "text/plain", "rele desconocido"); return; }
  r->send(200, "application/json", "{\"ok\":true}");
}

// Registro de eventos: ?n=40&saltar=0  (del más nuevo al más viejo)
static void enviarEventos(AsyncWebServerRequest* r) {
  long n      = r->hasParam("n")      ? r->getParam("n")->value().toInt()      : 40;
  long saltar = r->hasParam("saltar") ? r->getParam("saltar")->value().toInt() : 0;
  n      = constrain(n, 1L, 200L);
  saltar = constrain(saltar, 0L, 2000L);
  AsyncResponseStream* res = r->beginResponseStream("application/json", (size_t)n * 24 + 40);   // tamaño máximo: una sola reserva
  res->addHeader("Cache-Control", "no-store");
  eventosJson(*res, (uint16_t)n, (uint16_t)saltar);
  r->send(res);
}

static void enviarHistorial(AsyncWebServerRequest* r) {
  String rg = r->hasParam("rango") ? r->getParam("rango")->value() : "1d";
  uint8_t nivel = 0;
  if      (rg == "1w") nivel = 1;
  else if (rg == "1m") nivel = 2;
  else if (rg == "3m") nivel = 3;
  else if (rg == "1h") nivel = 4;

  AsyncResponseStream* res = r->beginResponseStream("application/json", historialJsonTam(nivel));   // tamaño máximo: una sola reserva
  res->addHeader("Cache-Control", "no-store");
  historialJson(nivel, *res);
  r->send(res);
}

// Autor y versión del software (pie de las páginas). Se cambian en config.h.
// Las rutas tienen que coincidir EXACTO. Con un texto simple la librería también atiende
// "/ruta/lo-que-sea" (ej.: "/api/borrar" se quedaba con "/api/borrar/bitacora").
#define RUTA(u) AsyncURIMatcher::exact(u)

static void enviarAcerca(AsyncWebServerRequest* r) {
  r->send(200, "application/json", "{\"autor\":\"" SOFTWARE_AUTOR "\",\"version\":\"" SOFTWARE_VERSION "\"}");
}

// Datos para saber qué le pasa al equipo si algo falla (se ven en la página, "Diagnóstico").
static void enviarDiag(AsyncWebServerRequest* r) {
  JsonDocument d;
  d["version"]         = VERSION_PROGRAMA;
  d["heapLibre"]       = ESP.getFreeHeap();
  d["heapBloqueMayor"] = ESP.getMaxAllocHeap();
  d["horaOk"]          = (time(nullptr) > 1700000000);
  d["uptime"]          = segEncendido();
  d["reinicio"]        = motivoReinicio();
  d["ip"]              = WiFi.localIP().toString();
  d["sinClave"]        = (bool)WEB_SIN_CLAVE;
  d["particion"]       = esp_ota_get_running_partition()->label;   // app0 / app1
  d["ota"]             = esp_ota_get_next_update_partition(nullptr) != nullptr;
  JsonArray h = d["historial"].to<JsonArray>();
  for (uint8_t i = 0; i < HIST_NIVELES; i++) h.add(historialCantidad(i));
  enviarJson(r, d);
}

// ---------- Diario de secado (los archivos y rutas conservan el nombre interno "bitacora") ----------
// GET  /api/bitacora       -> todo el archivo (una línea JSON por registro)
// POST /api/bitacora       -> linea=<json> agrega al final; con tmp=1 va al archivo temporal
//                             ini=1 vacía el temporal; fin=1 lo convierte en el diario
// GET  /api/bitacora/info  -> bytes usados y tope
// GET  /api/resumen        -> ?desde=<epoch>&hasta=<epoch> promedio/min/max de la cámara en ese intervalo
static void enviarBitacora(AsyncWebServerRequest* r) {
  if (!LittleFS.exists(BITACORA_ARCHIVO)) { r->send(200, "text/plain", ""); return; }
  r->send(LittleFS, BITACORA_ARCHIVO, "text/plain");
}

static void guardarBitacora(AsyncWebServerRequest* r) {
  auto tiene = [&](const char* k) { return r->hasParam(k, true); };
  if (tiene("ini")) {
    if (bitacoraTmpReiniciar()) r->send(200, "application/json", "{\"ok\":true}");
    else r->send(500, "application/json", "{\"ok\":false}");
    return;
  }
  if (tiene("fin")) {
    if (bitacoraTmpConfirmar()) r->send(200, "application/json", "{\"ok\":true}");
    else r->send(500, "application/json", "{\"ok\":false}");
    return;
  }
  if (!tiene("linea")) { r->send(400, "text/plain", "falta la linea"); return; }
  const int rc = bitacoraAgregar(r->getParam("linea", true)->value(), tiene("tmp"));
  if (rc == 0) {
    char b[48];
    snprintf(b, sizeof(b), "{\"ok\":true,\"tam\":%u}", (unsigned)bitacoraTam());
    r->send(200, "application/json", b);
  } else {
    r->send(rc, "application/json", rc == 507 ? "{\"ok\":false,\"error\":\"sin lugar\"}" : "{\"ok\":false,\"error\":\"linea invalida\"}");
  }
}

static void enviarBitacoraInfo(AsyncWebServerRequest* r) {
  JsonDocument d;
  d["tam"]   = (uint32_t)bitacoraTam();
  d["max"]   = (uint32_t)BITACORA_MAX_BYTES;
  enviarJson(r, d);
}

static void enviarResumen(AsyncWebServerRequest* r) {
  const uint32_t ahora = (uint32_t)time(nullptr);
  uint32_t hasta = r->hasParam("hasta") ? (uint32_t)r->getParam("hasta")->value().toInt() : ahora;
  uint32_t desde = r->hasParam("desde") ? (uint32_t)r->getParam("desde")->value().toInt() : (hasta > 86400 ? hasta - 86400 : 0);
  const HistResumen s = historialResumen(desde, hasta);
  JsonDocument d;
  d["n"] = s.n;
  if (s.n) {
    auto r1 = [](float v) { return roundf(v * 10.0f) / 10.0f; };
    d["tProm"] = r1(s.tProm); d["tMin"] = r1(s.tMin); d["tMax"] = r1(s.tMax);
    d["hProm"] = r1(s.hProm); d["hMin"] = r1(s.hMin); d["hMax"] = r1(s.hMax);
    d["pHum"] = (int)roundf(s.pHum); d["pDesh"] = (int)roundf(s.pDesh); d["pFrio"] = (int)roundf(s.pFrio);
  }
  enviarJson(r, d);
}

// Borra datos guardados. Exige confirmar=BORRAR.
// /api/borrar          -> historial y registro de relés
// /api/borrar/registro -> solo el registro de relés
// /api/borrar/bitacora -> solo el diario de secado
#if USAR_BALANZA
// Peso de la balanza (por Wi-Fi). Sin "g" = no hay peso válido (desconectada, sin HX711 o tarando).
static void enviarBalanza(AsyncWebServerRequest* r) {
  const BalanzaEstado b = balanzaEstado();
  JsonDocument d;
  d["con"] = b.conectada;
  if (b.hayPeso) d["g"] = roundf(b.gramos * 10.0f) / 10.0f;
  if (b.conectada) {
    d["est"]  = !!(b.flags & BAL_ESTABLE);
    d["sob"]  = !!(b.flags & BAL_SOBRECARGA);
    d["err"]  = !!(b.flags & BAL_SIN_HX711);
    d["cal"]  = !(b.flags & BAL_SIN_CALIB);
    d["ocup"] = !!(b.flags & BAL_OCUPADA);
    d["rssi"] = b.rssi;
  }
  d["msg"] = b.respuesta;

  // Temperatura y compensación (la calcula la balanza con la temperatura de esta placa)
  auto dec = [](float v, float f) { return roundf(v * f) / f; };
  if (b.conectada) {
    JsonObject tp = d["temp"].to<JsonObject>();
    if (!isnan(b.tCelda)) tp["celda"] = dec(b.tCelda, 10);
    if (est.sensorOk)     tp["aire"]  = dec(est.temp, 10);
    tp["corr"] = dec(b.corr, 10);
    tp["comp"] = b.comp;
    tp["kc"]   = dec(b.kCero, 1000);
    tp["ks"]   = dec(b.kSpan, 10000);
    if (!isnan(b.tTara)) tp["tara"]  = dec(b.tTara, 10);
    if (!isnan(b.tCal))  tp["calib"] = dec(b.tCal, 10);
    tp["avn"] = b.avisoNivel;
    tp["av"]  = b.aviso;
    if (b.aprendiendo) {
      JsonObject a = tp["apr"].to<JsonObject>();
      a["n"] = b.aprN;
      if (!isnan(b.aprDt))    a["dt"]    = dec(b.aprDt, 100);
      if (!isnan(b.aprPend))  a["pend"]  = dec(b.aprPend, 1000);
      if (!isnan(b.aprR2))    a["r2"]    = dec(b.aprR2, 100);
      if (!isnan(b.aprCarga)) a["carga"] = dec(b.aprCarga, 10);
    }
  }

  JsonObject red = d["red"].to<JsonObject>();
  red["activo"] = b.activo;
  red["estado"] = b.estadoCon;                // CON_*: 0 apagada, 1 buscando, 2 sin respuesta, 3 conectada
  red["ip"]     = b.vinculada;
  red["host"]   = BALANZA_HOST ".local";

  const PesajeEstado s = pesajeEstado();
  JsonObject sg = d["seg"].to<JsonObject>();
  sg["activo"] = s.activo;
  sg["pausa"]  = s.pausado;
  if (s.pausado) sg["pausaDesde"] = s.pausaDesde;
  sg["descolgada"] = s.descolgada;
  sg["p0"]     = s.p0;
  sg["obj"]    = s.objetivo;
  sg["ini"]    = s.inicio;
  sg["tanda"]  = s.tanda;
  sg["nombre"] = s.nombre;
  sg["puntos"] = s.puntos;
  sg["dias"]   = dec(s.dias, 100);
  sg["vivo"]   = s.enVivo;
  if (!isnan(s.peso))  sg["peso"]  = dec(s.peso, 10);
  if (!isnan(s.merma)) sg["merma"] = dec(s.merma, 100);
  if (!isnan(s.ritmo)) sg["ritmo"] = dec(s.ritmo, 1000);
  if (!isnan(s.falta)) sg["falta"] = dec(s.falta, 10);
  d["hora"] = (uint32_t)time(nullptr);
  enviarJson(r, d);
}

static void ordenConexion(AsyncWebServerRequest* r) {
  const String a = r->hasParam("accion", true) ? r->getParam("accion", true)->value() : String("");
  if      (a == "on")         balanzaActivar(true);
  else if (a == "off")        balanzaActivar(false);
  else if (a == "reconectar") balanzaReconectar();
  else if (a == "olvidar")    balanzaOlvidar();
  else { r->send(400, "application/json", "{\"ok\":false}"); return; }
  r->send(200, "application/json", "{\"ok\":true}");
}

// Seguimiento de la pieza testigo. Los números que no vienen no se cambian (al editar).
static void ordenSeguimiento(AsyncWebServerRequest* r) {
  auto texto = [&](const char* k) { return r->hasParam(k, true) ? r->getParam(k, true)->value() : String(""); };
  auto numero = [&](const char* k) { return r->hasParam(k, true) ? texto(k).toFloat() : NAN; };
  const String a = texto("accion");
  bool ok;
  if (a == "iniciar")
    ok = pesajeIniciar(numero("p0"), numero("obj"), (uint32_t)texto("ini").toInt(), texto("tanda").c_str(), texto("nombre").c_str());
  else if (a == "editar")
    ok = pesajeEditar(numero("p0"), numero("obj"),
                      r->hasParam("tanda", true) ? texto("tanda").c_str() : nullptr,
                      r->hasParam("nombre", true) ? texto("nombre").c_str() : nullptr);
  else if (a == "terminar") { pesajeTerminar(); ok = true; }
  else if (a == "pausar")   ok = pesajePausar(true);
  else if (a == "retomar")  ok = pesajePausar(false);
  else ok = false;
  r->send(ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

static void enviarPesaje(AsyncWebServerRequest* r) {
  AsyncResponseStream* res = r->beginResponseStream("application/json", pesajeJsonTam());
  res->addHeader("Cache-Control", "no-store");
  pesajeJson(*res);
  r->send(res);
}

// o=T | o=R | o=C<gramos> | o=A | o=AF | o=AX | o=M1 | o=M0, y t=<°C> opcional para T y C
// (temperatura de la tara / calibración; sin t usa la que estima la balanza). 409 si no está conectada.
static void ordenBalanza(AsyncWebServerRequest* r) {
  if (!r->hasParam("o", true)) { r->send(400, "application/json", "{\"ok\":false}"); return; }
  const String o = r->getParam("o", true)->value();
  const float g = o.substring(1).toFloat();
  const bool valida = o == "T" || o == "R" || o == "A" || o == "AF" || o == "AX" || o == "M1" || o == "M0" ||
                      (o.length() > 1 && o.length() < 12 && o[0] == 'C' && g > 0 && g <= 1000);
  float t = NAN;
  if (r->hasParam("t", true) && r->getParam("t", true)->value().length()) {
    t = r->getParam("t", true)->value().toFloat();
    if (!(t > -30 && t < 70)) { r->send(400, "application/json", "{\"ok\":false}"); return; }
  }
  if (!valida) { r->send(400, "application/json", "{\"ok\":false}"); return; }
  if (!balanzaOrden(o.c_str(), (o == "T" || o[0] == 'C') ? t : NAN)) { r->send(409, "application/json", "{\"ok\":false,\"sinConexion\":true}"); return; }
  r->send(200, "application/json", "{\"ok\":true}");
}
#endif

static bool borradoConfirmado(AsyncWebServerRequest* r) {
  if (!r->hasParam("confirmar", true) || r->getParam("confirmar", true)->value() != "BORRAR") {
    r->send(400, "application/json", "{\"ok\":false}");
    return false;
  }
  return true;
}
static void borrarDatos(AsyncWebServerRequest* r) {           // historial y registro de relés
  if (!borradoConfirmado(r)) return;
  historialBorrar();
  eventosBorrar();
  r->send(200, "application/json", "{\"ok\":true}");
}
static void borrarRegistro(AsyncWebServerRequest* r) {        // solo el registro de relés
  if (!borradoConfirmado(r)) return;
  eventosBorrar();
  r->send(200, "application/json", "{\"ok\":true}");
}
static void borrarBitacoraTodo(AsyncWebServerRequest* r) {    // solo el diario
  if (!borradoConfirmado(r)) return;
  bitacoraBorrar();
  r->send(200, "application/json", "{\"ok\":true}");
}

// ---------- Actualización del programa por Wi-Fi (OTA) ----------
// POST /api/ota  (multipart, un archivo .bin)  -> {"ok":true} y se reinicia a los 2 s
// La librería entrega el archivo ANTES de pasar por los middlewares (clave y X-Camara): por eso se
// controlan acá, con el primer pedazo. Mientras se graba, el clima queda apagado (controlOta).
// El programa nuevo arranca "a prueba": si se reinicia antes de confirmarse (webLoop), el ESP32
// vuelve solo al anterior.
static AsyncWebServerRequest* otaReq = nullptr;   // la subida en curso (una sola a la vez)
static bool     otaOk = false;                     // autorizada y grabando sin errores
static uint32_t reiniciarEn = 0;                   // millis() en que se pidió el reinicio (0 = no)

static void otaCancelar() {
  if (Update.isRunning()) Update.abort();
  otaReq = nullptr;
  otaOk = false;
  ControlCandado c;
  controlOta(false);
}

static void subidaOta(AsyncWebServerRequest* r, const String& nombre, size_t index, uint8_t* data, size_t len, bool final) {
  if (index == 0) {
    if (otaReq || reiniciarEn || !(WEB_SIN_CLAVE || acceso.allowed(r)) || !r->hasHeader("X-Camara")) return;   // la respuesta la da el middleware o finOta
    if (Update.isRunning()) Update.abort();
    { ControlCandado c; controlOta(true); }
    otaReq = r;                                     // aunque falle Update.begin: finOta informa el error
    otaOk = Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH);
    if (!otaOk) Update.printError(Serial);
    r->onDisconnect([r] { if (otaReq == r) { Serial.println("OTA: se corto la conexion"); otaCancelar(); } });
    Serial.printf("OTA: recibiendo %s\n", nombre.c_str());
  }
  if (r != otaReq || !otaOk) return;
  if (len && Update.write(data, len) != len) { Update.printError(Serial); otaOk = false; Update.abort(); }
  if (final && otaOk) {
    if (Update.end(true)) Serial.printf("OTA: %u bytes grabados\n", (unsigned)(index + len));
    else { Update.printError(Serial); otaOk = false; }
  }
}

static void finOta(AsyncWebServerRequest* r) {
  if (r != otaReq) { r->send(409, "application/json", "{\"ok\":false,\"error\":\"ocupado\"}"); return; }
  const bool ok = otaOk && Update.isFinished() && !Update.hasError();
  if (!ok) {
    const String err = Update.errorString();
    otaCancelar();
    r->send(500, "application/json", "{\"ok\":false,\"error\":\"" + err + "\"}");
    return;
  }
  otaReq = nullptr;
  reiniciarEn = millis() | 1;
  r->send(200, "application/json", "{\"ok\":true}");
}

// POST /api/parada  accion=on|off   PARADA DE EMERGENCIA: apaga los 6 relés y suspende el control (se guarda: un
// reinicio no la levanta). "off" la levanta. No depende de la pausa del control.
static void ordenParada(AsyncWebServerRequest* r) {
  const String a = r->hasParam("accion", true) ? r->getParam("accion", true)->value() : "";
  if (a != "on" && a != "off") { r->send(400, "application/json", "{\"ok\":false}"); return; }
  { ControlCandado c; controlParada(a == "on"); }
  r->send(200, "application/json", a == "on" ? "{\"ok\":true,\"parada\":true}" : "{\"ok\":true,\"parada\":false}");
}

// POST /api/reiniciar   Reinicia la ESP32 (vuelca antes el historial y los eventos). Los relés quedan apagados
// y el clima espera el tiempo mínimo apagado. Se rechaza durante una carga de programa y en el primer minuto
// después de una (el programa nuevo todavía no está confirmado: reiniciar lo haría volver al anterior).
static void ordenReiniciar(AsyncWebServerRequest* r) {
  esp_ota_img_states_t estado;
  const bool sinConfirmar = esp_ota_get_state_partition(esp_ota_get_running_partition(), &estado) == ESP_OK && estado == ESP_OTA_IMG_PENDING_VERIFY;
  if (otaReq || reiniciarEn || sinConfirmar) { r->send(409, "application/json", sinConfirmar ? "{\"ok\":false,\"motivo\":\"sin_confirmar\"}" : "{\"ok\":false,\"motivo\":\"ocupada\"}"); return; }
  reiniciarEn = millis() | 1;
  r->send(200, "application/json", "{\"ok\":true}");
}

// Lo llama el núcleo al arrancar: la confirmación del programa nuevo se hace después, en webLoop().
extern "C" bool verifyRollbackLater() { return true; }

void webLoop() {
  // Reinicio después de una actualización (se da tiempo a que salga la respuesta).
  if (reiniciarEn && millis() - reiniciarEn > 2000UL) {
    Serial.println("Reiniciando (actualización por Wi-Fi o pedido desde la web)");
    historialVolcar();
    eventosVolcar();
    delay(200);
    ESP.restart();
  }
  // Programa recién cargado por OTA: se confirma cuando lleva 1 minuto andando y con Wi-Fi.
  // Si a los 10 minutos sigue sin Wi-Fi (y no es el modo configuración), se vuelve al anterior.
  static bool revisado = false;
  if (revisado || millis() < 60000UL) return;
  esp_ota_img_states_t estado;
  if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &estado) != ESP_OK || estado != ESP_OTA_IMG_PENDING_VERIFY) {
    revisado = true;
    return;
  }
  if (WiFi.status() == WL_CONNECTED || wifiEnConfig()) {
    esp_ota_mark_app_valid_cancel_rollback();
    Serial.println("OTA: programa nuevo confirmado");
    revisado = true;
  } else if (millis() > 600000UL) {
    Serial.println("OTA: el programa nuevo no conecta al Wi-Fi, se vuelve al anterior");
    historialVolcar();
    eventosVolcar();
    esp_ota_mark_app_invalid_rollback_and_reboot();
  }
}

// Página guardada en la memoria de programa, ya comprimida con gzip (paginas/generar.py): se envía
// directo desde ahí, sin copiarla a la RAM, y el navegador la descomprime (pesa ~70 % menos).
static void enviarPagina(AsyncWebServerRequest* r, const uint8_t* gz, size_t n) {
  AsyncWebServerResponse* res = r->beginResponse(200, "text/html", gz, n);
  res->addHeader("Content-Encoding", "gzip");
  res->addHeader("Cache-Control", "no-cache");   // tras cargar un programa nuevo, el navegador no usa la página vieja
  r->send(res);
}
#define PAGINA(x) [](AsyncWebServerRequest* r) { enviarPagina(r, x, sizeof(x)); }

// ---------- Red Wi-Fi ----------
// GET  /api/wifi        -> red guardada, conexión, IP, señal y estado de la prueba (0 ninguna, 1 probando, 2 ok, 3 falló)
// GET  /api/wifi/scan   -> ?nuevo=1 pide una búsqueda nueva; responde {"estado":"buscando"} o {"estado":"listo","redes":[...]}
// POST /api/wifi        -> ssid=, clave=  prueba la red y, si conecta, la guarda
static void enviarWifi(AsyncWebServerRequest* r) {
  JsonDocument d;
  const bool con = (WiFi.status() == WL_CONNECTED);
  d["ssid"]      = wifiRedGuardada();
  d["conectado"] = con;
  d["ip"]        = con ? WiFi.localIP().toString() : String("");
  d["rssi"]      = con ? WiFi.RSSI() : 0;
  d["prueba"]    = wifiEstadoPrueba();
  enviarJson(r, d);
}

static void escanearWifi(AsyncWebServerRequest* r) {
  if (r->hasParam("nuevo")) wifiPedirBusqueda();   // la hace wifiLoop() (sin conexión hay que pausar los reintentos)
  JsonDocument d;
  if (wifiBuscando()) {
    d["estado"] = "buscando";
  } else {
    const int n = WiFi.scanComplete();             // < 0: la búsqueda falló; se responde la lista vacía
    struct Red { String s; int r; bool a; };
    std::vector<Red> v;
    for (int i = 0; i < n; i++) {
      String s = WiFi.SSID(i);
      if (s.length() == 0) continue;
      bool hay = false;
      for (auto& x : v) if (x.s == s) { hay = true; if (WiFi.RSSI(i) > x.r) x.r = WiFi.RSSI(i); }
      if (!hay) v.push_back({ s, (int)WiFi.RSSI(i), WiFi.encryptionType(i) == WIFI_AUTH_OPEN });
    }
    std::sort(v.begin(), v.end(), [](const Red& a, const Red& b) { return a.r > b.r; });
    if (v.size() > 20) v.resize(20);
    d["estado"] = "listo";
    JsonArray a = d["redes"].to<JsonArray>();
    for (auto& x : v) { JsonObject o = a.add<JsonObject>(); o["s"] = x.s; o["r"] = x.r; o["a"] = x.a; }
  }
  enviarJson(r, d);
}

static void guardarWifi(AsyncWebServerRequest* r) {
  if (!r->hasParam("ssid", true)) { r->send(400, "application/json", "{\"ok\":false}"); return; }
  const String ssid  = r->getParam("ssid", true)->value();
  const String clave = r->hasParam("clave", true) ? r->getParam("clave", true)->value() : String("");
  if (wifiProbar(ssid.c_str(), clave.c_str())) r->send(200, "application/json", "{\"ok\":true}");
  else r->send(400, "application/json", "{\"ok\":false}");
}

void webInit() {
  // Con WEB_SIN_CLAVE 0 todo pide clave; las escrituras llevan siempre el encabezado anti-CSRF.
  accesoInit();
#if !WEB_SIN_CLAVE
  server.addMiddleware(&acceso);
#endif
  server.addMiddleware(exigirEncabezado);

  // Chart.js propio (sin depender de internet ni de un CDN)
  server.on(RUTA("/chart.js"), HTTP_GET, [](AsyncWebServerRequest* r) {
    AsyncWebServerResponse* resp = r->beginResponse(200, "application/javascript", CHARTJS_GZ, CHARTJS_GZ_LEN);
    resp->addHeader("Content-Encoding", "gzip");
    resp->addHeader("Cache-Control", "max-age=31536000");
    r->send(resp);
  });
  server.on(RUTA("/"),         HTTP_GET, PAGINA(PAGINA_HTML_GZ));
  server.on(RUTA("/bitacora"), HTTP_GET, PAGINA(PAGINA_BITACORA_HTML_GZ));
  server.on(RUTA("/wifi"),     HTTP_GET, PAGINA(PAGINA_WIFI_HTML_GZ));
  server.on(RUTA("/api/bitacora"),      HTTP_GET,  enviarBitacora);
  server.on(RUTA("/api/bitacora"),      HTTP_POST, guardarBitacora);
  server.on(RUTA("/api/bitacora/info"), HTTP_GET,  enviarBitacoraInfo);
  server.on(RUTA("/api/acerca"),        HTTP_GET,  enviarAcerca);
  server.on(RUTA("/api/resumen"),       HTTP_GET,  enviarResumen);
  server.on(RUTA("/api/wifi"),          HTTP_GET,  enviarWifi);
  server.on(RUTA("/api/wifi/scan"),     HTTP_GET,  escanearWifi);
  server.on(RUTA("/api/wifi"),          HTTP_POST, guardarWifi);
  server.on(RUTA("/api/borrar"),        HTTP_POST, borrarDatos);
  server.on(RUTA("/api/borrar/bitacora"), HTTP_POST, borrarBitacoraTodo);
  server.on(RUTA("/api/borrar/registro"), HTTP_POST, borrarRegistro);
  server.on(RUTA("/api/estado"),    HTTP_GET,  enviarEstado);
  server.on(RUTA("/api/config"),    HTTP_POST, guardarConfig);
  server.on(RUTA("/api/manual"),    HTTP_POST, cambiarManual);
  server.on(RUTA("/api/eventos"),   HTTP_GET,  enviarEventos);
  server.on(RUTA("/api/historial"), HTTP_GET,  enviarHistorial);
  server.on(RUTA("/api/diag"),      HTTP_GET,  enviarDiag);
  server.on(RUTA("/api/test"),      HTTP_GET,  enviarTest);
  server.on(RUTA("/api/test"),      HTTP_POST, ordenTest);
  server.on(RUTA("/api/calib"),     HTTP_POST, ordenCalib);
  server.on(RUTA("/api/clave"),     HTTP_POST, cambiarClave);
  server.on(RUTA("/api/ota"),       HTTP_POST, finOta, subidaOta);
  server.on(RUTA("/api/parada"),    HTTP_POST, ordenParada);
  server.on(RUTA("/api/reiniciar"), HTTP_POST, ordenReiniciar);
#if USAR_BALANZA
  server.on(RUTA("/api/balanza"),   HTTP_GET,  enviarBalanza);
  server.on(RUTA("/api/balanza"),   HTTP_POST, ordenBalanza);
  server.on(RUTA("/api/balanza/red"),  HTTP_POST, ordenConexion);
  server.on(RUTA("/api/balanza/seg"),  HTTP_POST, ordenSeguimiento);
  server.on(RUTA("/api/balanza/hist"), HTTP_GET,  enviarPesaje);
#endif
  server.onNotFound([](AsyncWebServerRequest* r) {
    if (wifiEnConfig()) { r->redirect("http://" WIFI_AP_IP "/wifi"); return; }   // portal: cualquier dirección lleva a la configuración
    r->send(404, "text/plain", "No encontrado");
  });
  server.begin();
}
