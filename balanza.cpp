#include "balanza.h"
#if USAR_BALANZA
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <algorithm>
#include "control.h"                      // est.temp: se le manda a la balanza para compensar

// Una entrada por balanza (BALANZAS[], hardware.h). El índice 0 es la de siempre: al conservar su
// namespace de NVS y su archivo de historial, todo lo ya registrado sigue igual.
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static BalanzaEstado st[BALANZAS_MAX];
static uint32_t ultDato[BALANZAS_MAX] = {0};
static char ordenPend[BALANZAS_MAX][32];
static bool esperandoResp[BALANZAS_MAX] = {false};
static uint32_t ultBusqueda[BALANZAS_MAX] = {0};
static volatile bool pedirBusqueda[BALANZAS_MAX] = {false};
static volatile bool pedirOlvido[BALANZAS_MAX] = {false};
static Preferences pb[BALANZAS_MAX];

#define INTERVALO_MS   3000UL             // una consulta cada 3 s (una por segundo cargaba al ESP32: perdía paquetes)
#define INTERVALO_RAPIDO_MS 1000UL        // mientras espera la respuesta de una orden
#define INTERVALO_OFF_MS 10000UL          // una balanza que no contesta: cada 10 s (no vale la pena más)
#define SIN_DATOS_MS   10000UL            // sin respuestas en este tiempo -> "sin respuesta"
#define REBUSCAR_MS    30000UL            // sin respuestas en este tiempo -> se busca de nuevo por nombre
#define BUSCAR_MS      60000UL            // cuánto se espera entre búsquedas por mDNS (cada una bloquea la tarea)

static void ponerCon(uint8_t i, uint8_t e) { portENTER_CRITICAL(&mux); st[i].estadoCon = e; portEXIT_CRITICAL(&mux); }

static void guardarIp(uint8_t i, const String& ip) {
  portENTER_CRITICAL(&mux);
  const bool nueva = strcmp(st[i].vinculada, ip.c_str()) != 0;
  if (nueva) strlcpy(st[i].vinculada, ip.c_str(), sizeof(st[i].vinculada));
  portEXIT_CRITICAL(&mux);
  if (nueva) {
    pb[i].putString("ip", ip);
    Serial.printf("Balanza %s: encontrada en %s\n", BALANZAS[i].nombre, ip.c_str());
  }
}

// Busca la balanza por nombre (mDNS). true si la encontró (y guarda la IP).
static bool buscar(uint8_t i) {
  IPAddress ip = MDNS.queryHost(BALANZAS[i].host, 1200);
  if (ip == IPAddress((uint32_t)0) && BALANZAS[i].alt && BALANZAS[i].alt[0]) {
    Serial.printf("Balanza %s: %s.local no aparece, pruebo %s.local\n",
                  BALANZAS[i].nombre, BALANZAS[i].host, BALANZAS[i].alt);
    ip = MDNS.queryHost(BALANZAS[i].alt, 1200);
  }
  if (ip == IPAddress((uint32_t)0)) return false;
  guardarIp(i, ip.toString());
  return true;
}

static float numONan(JsonVariantConst v) { return v.is<float>() ? v.as<float>() : NAN; }

// ---------- HTTP mínimo sobre TCP ----------
// No se usa HTTPClient: trae soporte para https:// y con eso ~95 KB de cifrado que no hacen falta
// (las balanzas están en la red de casa, por http común). Ese lugar es el que permite tener OTA.
#define HTTP_TIMEOUT_MS 1500UL
static char resp[1280];                   // respuesta completa (cabecera + cuerpo); solo la usa la tarea

// Manda "<metodo> <ruta>" (con el cuerpo, si hay) y espera la respuesta.
// Devuelve el código HTTP (-1 si no conectó o no contestó) y deja en *cuerpoResp el cuerpo (dentro de resp).
static int pedir(const char* ip, const char* metodo, const char* ruta, const char* cuerpo, const char** cuerpoResp) {
  WiFiClient c;
  if (!c.connect(ip, 80, HTTP_TIMEOUT_MS)) return -1;
  char cab[256];
  int n = snprintf(cab, sizeof(cab), "%s %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n", metodo, ruta, ip);
  if (cuerpo) n += snprintf(cab + n, sizeof(cab) - n, "Content-Type: application/x-www-form-urlencoded\r\nContent-Length: %u\r\n",
                            (unsigned)strlen(cuerpo));
  n += snprintf(cab + n, sizeof(cab) - n, "\r\n");
  if (n >= (int)sizeof(cab)) return -1;
  c.write((const uint8_t*)cab, n);
  if (cuerpo) c.write((const uint8_t*)cuerpo, strlen(cuerpo));

  // Se lee hasta que la balanza cierra, o hasta tener el cuerpo entero (Content-Length).
  size_t len = 0;
  const char* cuerpoIni = nullptr;
  long largo = -1;
  const uint32_t t0 = millis();
  while (millis() - t0 < HTTP_TIMEOUT_MS && len < sizeof(resp) - 1) {
    const int hay = c.available();
    if (hay <= 0) {
      if (!c.connected()) break;
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }
    len += c.read((uint8_t*)resp + len, std::min((size_t)hay, sizeof(resp) - 1 - len));
    resp[len] = 0;
    if (!cuerpoIni && (cuerpoIni = strstr(resp, "\r\n\r\n"))) {
      cuerpoIni += 4;
      const char* cl = strcasestr(resp, "\r\nContent-Length:");
      if (cl && cl < cuerpoIni) largo = atol(cl + 17);
    }
    if (cuerpoIni && largo >= 0 && (long)(resp + len - cuerpoIni) >= largo) break;
  }
  c.stop();
  resp[len] = 0;
  int codigo = -1;
  if (len > 12 && strncmp(resp, "HTTP/1.", 7) == 0) codigo = atoi(resp + 9);
  if (!cuerpoIni) return -1;              // cabecera incompleta
  if (cuerpoResp) *cuerpoResp = cuerpoIni;
  return codigo;
}

// La placa que contestó no es la esperada (IP vieja, nombre repetido por mDNS): se olvida la IP y no se
// toma ni un dato ni una orden de ella hasta encontrar la correcta.
static void placaEquivocada(uint8_t i, const char* ip, const char* quien) {
  Serial.printf("Balanza %s: en %s contesta \"%s\", no es ella: se descarta\n", BALANZAS[i].nombre, ip, quien[0] ? quien : "sin nombre");
  portENTER_CRITICAL(&mux);
  st[i].vinculada[0] = 0;
  ultDato[i] = 0;
  portEXIT_CRITICAL(&mux);
  pb[i].remove("ip");
}

// Pide el peso (y le pasa la temperatura de la cámara). true si la balanza contestó bien.
static bool consultar(uint8_t i, const char* ip, bool& esperando) {
  char ruta[48];
  const float t = est.temp;
  if (est.sensorOk && isfinite(t)) snprintf(ruta, sizeof(ruta), "/api/peso?cam=1&t=%.2f", t);
  else                             snprintf(ruta, sizeof(ruta), "/api/peso?cam=1");
  const char* cuerpo = nullptr;
  if (pedir(ip, "GET", ruta, nullptr, &cuerpo) != 200) return false;
  JsonDocument d;
  if (deserializeJson(d, cuerpo) != DeserializationError::Ok) return false;
  const char* quien = d["host"] | "";   // las placas nuevas dicen su nombre; la de 1 kg vieja no
  if ((quien[0] && strcasecmp(quien, BALANZAS[i].host) != 0) || (!quien[0] && BALANZAS[i].exigeId)) { placaEquivocada(i, ip, quien); return false; }
  uint8_t f = 0;
  if (d["est"].as<int>())  f |= BAL_ESTABLE;
  if (d["sob"].as<int>())  f |= BAL_SOBRECARGA;
  if (d["err"].as<int>())  f |= BAL_SIN_HX711;
  if (!d["cal"].as<int>()) f |= BAL_SIN_CALIB;
  if (d["ocup"].as<int>()) f |= BAL_OCUPADA;
  const bool hayG = d["g"].is<float>();
  const char* msg = d["msg"] | "";
  const char* av  = d["av"] | "";
  JsonVariantConst a = d["apr"];
  const float tc = numONan(d["tc"]), tt = numONan(d["tt"]), tk = numONan(d["tk"]);
  const float aDt = numONan(a["dt"]), aPend = numONan(a["pend"]), aR2 = numONan(a["r2"]), aCarga = numONan(a["carga"]);
  portENTER_CRITICAL(&mux);
  st[i].flags  = f;
  st[i].gramos = hayG ? d["g"].as<float>() : NAN;
  st[i].rssi   = d["rssi"] | 0;
  st[i].tCelda = tc;
  st[i].corr   = d["corr"] | 0.0f;
  st[i].comp   = d["comp"] | 0;
  st[i].kCero  = d["kc"] | 0.0f;
  st[i].kSpan  = d["ks"] | 0.0f;
  st[i].tTara  = tt;
  st[i].tCal   = tk;
  st[i].avisoNivel = d["avn"] | 0;
  strlcpy(st[i].aviso, av, sizeof(st[i].aviso));
  st[i].aprendiendo = a.is<JsonObjectConst>();
  st[i].aprN     = a["n"] | 0;
  st[i].aprDt    = aDt;
  st[i].aprPend  = aPend;
  st[i].aprR2    = aR2;
  st[i].aprCarga = aCarga;
  ultDato[i] = millis();
  // La respuesta de una orden: cuando la balanza terminó (no está ocupada) y hay mensaje.
  if (esperando && !(f & BAL_OCUPADA) && msg[0]) {
    strlcpy(st[i].respuesta, msg, sizeof(st[i].respuesta));
    esperando = false;
  }
  portEXIT_CRITICAL(&mux);
  return true;
}

static bool enviarOrden(const char* ip, const char* o) {
  char cuerpo[40];
  snprintf(cuerpo, sizeof(cuerpo), "o=%s", o);
  return pedir(ip, "POST", "/api/orden", cuerpo, nullptr) == 200;
}

// Atiende una balanza: búsqueda por nombre si hace falta, orden pendiente y consulta del peso.
static void atender(uint8_t i) {
  if (pedirOlvido[i]) {
    pedirOlvido[i] = false;
    pb[i].remove("ip");
    portENTER_CRITICAL(&mux);
    st[i].vinculada[0] = 0;
    ultDato[i] = 0;
    portEXIT_CRITICAL(&mux);
    pedirBusqueda[i] = true;
  }
  if (!st[i].activo) { ponerCon(i, CON_APAGADO); return; }
  if (WiFi.status() != WL_CONNECTED) { ponerCon(i, CON_BUSCANDO); return; }

  // Sin IP, o sin respuestas hace rato: buscar por nombre (como mucho cada 10 s).
  const bool callada = !ultDato[i] || millis() - ultDato[i] > REBUSCAR_MS;
  if (pedirBusqueda[i] || ((!st[i].vinculada[0] || callada) && millis() - ultBusqueda[i] > BUSCAR_MS)) {
    pedirBusqueda[i] = false;
    ultBusqueda[i] = millis();
    if (!st[i].vinculada[0]) ponerCon(i, CON_BUSCANDO);
    buscar(i);
  }
  char ip[18];
  portENTER_CRITICAL(&mux);
  strcpy(ip, st[i].vinculada);
  portEXIT_CRITICAL(&mux);
  if (!ip[0]) return;

  char o[sizeof(ordenPend[0])];
  portENTER_CRITICAL(&mux);
  strcpy(o, ordenPend[i]);
  ordenPend[i][0] = 0;
  portEXIT_CRITICAL(&mux);
  if (o[0]) {
    if (enviarOrden(ip, o)) esperandoResp[i] = true;
    else {
      portENTER_CRITICAL(&mux);
      strlcpy(st[i].respuesta, "ERR no se pudo enviar", sizeof(st[i].respuesta));
      portEXIT_CRITICAL(&mux);
    }
  }

  consultar(i, ip, esperandoResp[i]);
  ponerCon(i, ultDato[i] && millis() - ultDato[i] < SIN_DATOS_MS ? CON_CONECTADA : CON_SIN_RESPUESTA);
}

static void tarea(void*) {
  uint32_t ultConsulta[BALANZAS_MAX] = {0};

  for (;;) {
    bool algo = false;
    for (uint8_t i = 0; i < BALANZAS_MAX; i++) {
      const bool viva = ultDato[i] && millis() - ultDato[i] < SIN_DATOS_MS;
      const uint32_t espera = esperandoResp[i] ? INTERVALO_RAPIDO_MS : (viva ? INTERVALO_MS : INTERVALO_OFF_MS);
      // Una balanza apagada o sin IP se atiende igual (para buscar), pero sin apurar a las demás.
      if (millis() - ultConsulta[i] < espera && !ordenPend[i][0] && !pedirBusqueda[i] && !pedirOlvido[i]) continue;
      ultConsulta[i] = millis();
      atender(i);
      algo = true;
    }
    // Queda liviana: si no hubo nada que hacer, duerme 250 ms; si trabajó, apenas 20 ms.
    vTaskDelay(pdMS_TO_TICKS(algo ? 20 : 250));
  }
}

uint8_t balanzaCantidad() { return BALANZAS_MAX; }
const char* balanzaHost(uint8_t i) { return BALANZAS[i < BALANZAS_MAX ? i : 0].host; }
const char* balanzaNombre(uint8_t i) { return BALANZAS[i < BALANZAS_MAX ? i : 0].nombre; }
float balanzaCapacidad(uint8_t i) { return BALANZAS[i < BALANZAS_MAX ? i : 0].capacidad; }

void balanzaInit() {
  char ns[16];
  for (uint8_t i = 0; i < BALANZAS_MAX; i++) {
    // El índice 0 conserva el namespace histórico para no perder la IP ni el estado guardados.
    if (i == 0) strlcpy(ns, "balanzaRed", sizeof(ns));
    else        snprintf(ns, sizeof(ns), "balanzaRed%u", i);
    pb[i].begin(ns, false);
    st[i].activo = pb[i].getBool("activo", true);
    st[i].gramos = st[i].tCelda = st[i].tTara = st[i].tCal = NAN;
    strlcpy(st[i].vinculada, pb[i].getString("ip", "").c_str(), sizeof(st[i].vinculada));
    ordenPend[i][0] = 0;
  }
  // Prioridad 0 (la del idle): el loop() de Arduino (prioridad 1) siempre gana. Con una balanza
  // apagada, las búsquedas por mDNS y los timeouts de conexión bloquean esta tarea, no la web.
  xTaskCreatePinnedToCore(tarea, "balanza", 6144, nullptr, 0, nullptr, 1);
}

BalanzaEstado balanzaEstado(uint8_t i) {
  if (i >= BALANZAS_MAX) i = 0;
  portENTER_CRITICAL(&mux);
  BalanzaEstado e = st[i];
  e.conectada = st[i].activo && ultDato[i] && millis() - ultDato[i] < SIN_DATOS_MS;
  portEXIT_CRITICAL(&mux);
  e.hayPeso = e.conectada && !isnan(e.gramos) && !(e.flags & (BAL_SIN_HX711 | BAL_OCUPADA));
  if (!e.conectada) e.rssi = 0;
  return e;
}

bool balanzaOrden(const char* orden, float t, uint8_t i) {
  if (i >= BALANZAS_MAX) i = 0;
  if (!balanzaEstado(i).conectada) return false;
  char o[sizeof(ordenPend[0])];
  if (isfinite(t)) snprintf(o, sizeof(o), "%s&t=%.1f", orden, t);
  else             strlcpy(o, orden, sizeof(o));
  bool ok = false;
  portENTER_CRITICAL(&mux);
  if (!ordenPend[i][0]) {
    strlcpy(ordenPend[i], o, sizeof(ordenPend[i]));
    st[i].respuesta[0] = 0;               // la respuesta llega en las próximas consultas
    ok = true;
  }
  portEXIT_CRITICAL(&mux);
  return ok;
}

void balanzaActivar(bool on, uint8_t i) {
  if (i >= BALANZAS_MAX) i = 0;
  if (on == st[i].activo) return;
  portENTER_CRITICAL(&mux);
  st[i].activo = on;
  if (!on) ultDato[i] = 0;
  portEXIT_CRITICAL(&mux);
  pb[i].putBool("activo", on);
}

void balanzaReconectar(uint8_t i) { if (i < BALANZAS_MAX) pedirBusqueda[i] = true; }
void balanzaOlvidar(uint8_t i)    { if (i < BALANZAS_MAX) pedirOlvido[i] = true; }
#endif
