#include "balanza.h"
#if USAR_BALANZA
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <algorithm>
#include "control.h"                      // est.temp: se le manda a la balanza para compensar

// El estado lo escribe la tarea de la balanza y lo leen la web, el menú y el loop(): todo bajo este candado.
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static BalanzaEstado st = {};
static uint32_t ultDato = 0;              // millis() de la última respuesta buena
static char ordenPend[32] = "";           // orden a enviar, ya como cuerpo del POST sin "o=" (la manda la tarea)
static volatile bool pedirBusqueda = false;   // reconectar: buscar de nuevo por nombre
static volatile bool pedirOlvido = false;
static Preferences pb;                    // NVS "balanzaRed": activo e IP guardada

#define INTERVALO_MS   3000UL             // una consulta cada 3 s (una por segundo cargaba al ESP32: perdía paquetes)
#define INTERVALO_RAPIDO_MS 1000UL        // mientras espera la respuesta de una orden
#define SIN_DATOS_MS   10000UL            // sin respuestas en este tiempo -> "sin respuesta"
#define REBUSCAR_MS    30000UL            // sin respuestas en este tiempo -> se busca de nuevo por nombre

static void ponerCon(uint8_t e) { portENTER_CRITICAL(&mux); st.estadoCon = e; portEXIT_CRITICAL(&mux); }

static void guardarIp(const String& ip) {
  portENTER_CRITICAL(&mux);
  const bool nueva = strcmp(st.vinculada, ip.c_str()) != 0;
  if (nueva) strlcpy(st.vinculada, ip.c_str(), sizeof(st.vinculada));
  portEXIT_CRITICAL(&mux);
  if (nueva) { pb.putString("ip", ip); Serial.printf("Balanza: encontrada en %s\n", ip.c_str()); }
}

// Busca la balanza por nombre (mDNS). true si la encontró (y guarda la IP).
static bool buscar() {
  const IPAddress ip = MDNS.queryHost(BALANZA_HOST, 2000);
  if (ip == IPAddress((uint32_t)0)) return false;
  guardarIp(ip.toString());
  return true;
}

static float numONan(JsonVariantConst v) { return v.is<float>() ? v.as<float>() : NAN; }

// ---------- HTTP mínimo sobre TCP ----------
// No se usa HTTPClient: trae soporte para https:// y con eso ~95 KB de cifrado que no hacen falta
// (la balanza está en la red de casa, por http común). Ese lugar es el que permite tener OTA.
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

// Pide el peso (y le pasa la temperatura de la cámara). true si la balanza contestó bien.
static bool consultar(const char* ip, bool& esperandoResp) {
  char ruta[48];
  const float t = est.temp;
  if (est.sensorOk && isfinite(t)) snprintf(ruta, sizeof(ruta), "/api/peso?cam=1&t=%.2f", t);
  else                             snprintf(ruta, sizeof(ruta), "/api/peso?cam=1");
  const char* cuerpo = nullptr;
  if (pedir(ip, "GET", ruta, nullptr, &cuerpo) != 200) return false;
  JsonDocument d;
  if (deserializeJson(d, cuerpo) != DeserializationError::Ok) return false;
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
  st.flags  = f;
  st.gramos = hayG ? d["g"].as<float>() : NAN;
  st.rssi   = d["rssi"] | 0;
  st.tCelda = tc;
  st.corr   = d["corr"] | 0.0f;
  st.comp   = d["comp"] | 0;
  st.kCero  = d["kc"] | 0.0f;
  st.kSpan  = d["ks"] | 0.0f;
  st.tTara  = tt;
  st.tCal   = tk;
  st.avisoNivel = d["avn"] | 0;
  strlcpy(st.aviso, av, sizeof(st.aviso));
  st.aprendiendo = a.is<JsonObjectConst>();
  st.aprN     = a["n"] | 0;
  st.aprDt    = aDt;
  st.aprPend  = aPend;
  st.aprR2    = aR2;
  st.aprCarga = aCarga;
  ultDato   = millis();
  // La respuesta de una orden: cuando la balanza terminó (no está ocupada) y hay mensaje.
  if (esperandoResp && !(f & BAL_OCUPADA) && msg[0]) {
    strlcpy(st.respuesta, msg, sizeof(st.respuesta));
    esperandoResp = false;
  }
  portEXIT_CRITICAL(&mux);
  return true;
}

static bool enviarOrden(const char* ip, const char* o) {
  char cuerpo[40];
  snprintf(cuerpo, sizeof(cuerpo), "o=%s", o);
  return pedir(ip, "POST", "/api/orden", cuerpo, nullptr) == 200;
}

static void tarea(void*) {
  bool esperandoResp = false;
  uint32_t ultBusqueda = 0;

  for (;;) {
    // Espera de a 250 ms: si llega una orden o un pedido de búsqueda, se atiende enseguida.
    for (uint32_t t = 0; t < (esperandoResp ? INTERVALO_RAPIDO_MS : INTERVALO_MS); t += 250) {
      vTaskDelay(pdMS_TO_TICKS(250));
      if (ordenPend[0] || pedirBusqueda || pedirOlvido) break;
    }

    if (pedirOlvido) {
      pedirOlvido = false;
      pb.remove("ip");
      portENTER_CRITICAL(&mux);
      st.vinculada[0] = 0;
      ultDato = 0;
      portEXIT_CRITICAL(&mux);
      pedirBusqueda = true;
    }
    if (!st.activo) { ponerCon(CON_APAGADO); continue; }
    if (WiFi.status() != WL_CONNECTED) { ponerCon(CON_BUSCANDO); continue; }

    // Sin IP, o sin respuestas hace rato: buscar por nombre (como mucho cada 10 s).
    const bool callada = !ultDato || millis() - ultDato > REBUSCAR_MS;
    if (pedirBusqueda || ((!st.vinculada[0] || callada) && millis() - ultBusqueda > 10000UL)) {
      pedirBusqueda = false;
      ultBusqueda = millis();
      if (!st.vinculada[0]) ponerCon(CON_BUSCANDO);
      buscar();
    }
    char ip[18];
    portENTER_CRITICAL(&mux);
    strcpy(ip, st.vinculada);
    portEXIT_CRITICAL(&mux);
    if (!ip[0]) continue;

    char o[sizeof(ordenPend)];
    portENTER_CRITICAL(&mux);
    strcpy(o, ordenPend);
    ordenPend[0] = 0;
    portEXIT_CRITICAL(&mux);
    if (o[0]) {
      if (enviarOrden(ip, o)) esperandoResp = true;
      else { portENTER_CRITICAL(&mux); strlcpy(st.respuesta, "ERR no se pudo enviar", sizeof(st.respuesta)); portEXIT_CRITICAL(&mux); }
    }

    consultar(ip, esperandoResp);
    ponerCon(ultDato && millis() - ultDato < SIN_DATOS_MS ? CON_CONECTADA : CON_SIN_RESPUESTA);
  }
}

void balanzaInit() {
  pb.begin("balanzaRed", false);
  st.activo = pb.getBool("activo", true);
  st.gramos = st.tCelda = st.tTara = st.tCal = NAN;
  strlcpy(st.vinculada, pb.getString("ip", "").c_str(), sizeof(st.vinculada));
  xTaskCreatePinnedToCore(tarea, "balanza", 6144, nullptr, 1, nullptr, 1);
}

BalanzaEstado balanzaEstado() {
  portENTER_CRITICAL(&mux);
  BalanzaEstado e = st;
  e.conectada = st.activo && ultDato && millis() - ultDato < SIN_DATOS_MS;
  portEXIT_CRITICAL(&mux);
  e.hayPeso = e.conectada && !isnan(e.gramos) && !(e.flags & (BAL_SIN_HX711 | BAL_OCUPADA));
  if (!e.conectada) e.rssi = 0;
  return e;
}

bool balanzaOrden(const char* orden, float t) {
  if (!balanzaEstado().conectada) return false;
  char o[sizeof(ordenPend)];
  if (isfinite(t)) snprintf(o, sizeof(o), "%s&t=%.1f", orden, t);
  else             strlcpy(o, orden, sizeof(o));
  bool ok = false;
  portENTER_CRITICAL(&mux);
  if (!ordenPend[0]) {
    strlcpy(ordenPend, o, sizeof(ordenPend));
    st.respuesta[0] = 0;                  // la respuesta llega en las próximas consultas
    ok = true;
  }
  portEXIT_CRITICAL(&mux);
  return ok;
}

void balanzaActivar(bool on) {
  if (on == st.activo) return;
  portENTER_CRITICAL(&mux);
  st.activo = on;
  if (!on) ultDato = 0;
  portEXIT_CRITICAL(&mux);
  pb.putBool("activo", on);
}

void balanzaReconectar() { pedirBusqueda = true; }
void balanzaOlvidar()    { pedirOlvido = true; }
#endif
