#include "redwifi.h"
#include "config.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include <Preferences.h>

static Preferences pw;
static DNSServer   dns;

static char     ssidG[33] = "";          // red guardada (la que se usa al arrancar)
static char     claveG[64] = "";
static bool     apActivo = false, quiereAp = false, mdnsOk = false;
static volatile bool quiereCerrar = false;
static uint32_t apCierre = 0;            // millis() a partir del cual se puede cerrar el modo configuración
static uint32_t sinConexion = 0;         // millis() desde que se perdió la conexión (0 = conectado)
static uint32_t ultReintento = 0;

static volatile uint8_t prueba = WF_SIN_PRUEBA;
static volatile bool    pedido = false;
static char     pSsid[33], pClave[64];
static uint32_t pInicio = 0;

static volatile bool busPedida = false;  // búsqueda de redes pedida desde la web
static bool     busEnCurso = false, busPausoSta = false;
static uint32_t busInicio = 0;
static uint8_t  busIntentos = 0;

static void cargar() {
  pw.begin("redwifi", false);
  String s = pw.getString("ssid", "");
  String c = pw.getString("clave", "");
  pw.end();
  if (s.length() == 0) { s = WIFI_SSID; c = WIFI_PASS; }
  snprintf(ssidG, sizeof(ssidG), "%s", s.c_str());
  snprintf(claveG, sizeof(claveG), "%s", c.c_str());
}

static void guardar(const char* s, const char* c) {
  pw.begin("redwifi", false);
  pw.putString("ssid", s);
  pw.putString("clave", c);
  pw.end();
  snprintf(ssidG, sizeof(ssidG), "%s", s);
  snprintf(claveG, sizeof(claveG), "%s", c);
}

static void levantarAp() {
  if (apActivo) return;
  WiFi.mode(WIFI_AP_STA);                // la conexión a la red de la casa sigue funcionando
  WiFi.softAP(WIFI_AP_NOMBRE);           // red abierta: solo existe mientras dura el modo configuración
  dns.start(53, "*", WiFi.softAPIP());   // portal: cualquier nombre lleva a la placa
  apActivo = true;
  apCierre = millis() + 600000UL;        // se cierra a los 10 min si ya hay conexión
  Serial.printf("Modo configuracion Wi-Fi: red \"%s\", pagina http://%s/wifi\n", WIFI_AP_NOMBRE, WIFI_AP_IP);
}

static void bajarAp() {
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  apActivo = false;
  Serial.println("Modo configuracion Wi-Fi cerrado");
}

void wifiInit(bool forzarConfig) {
  cargar();
  WiFi.setHostname(HOSTNAME);   // antes de WiFi.mode(): si no, el router muestra "esp32-xxxxxx"
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssidG, claveG);

  // Espera acotada: el control funciona aunque no haya Wi-Fi.
  const uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
    delay(250);
    Serial.print('.');
  }
  if (WiFi.status() == WL_CONNECTED) Serial.printf("\nWi-Fi OK (%s). IP: %s\n", ssidG, WiFi.localIP().toString().c_str());
  else Serial.println("\nSin Wi-Fi por ahora; sigue reintentando en segundo plano.");

  const bool sinConfigurar = (strcmp(ssidG, "TU_RED_WIFI") == 0);
  if (forzarConfig || sinConfigurar) levantarAp();
  sinConexion = (WiFi.status() == WL_CONNECTED) ? 0 : (millis() | 1);
}

void wifiIniciarConfig() { quiereAp = true; quiereCerrar = false; }
void wifiCerrarConfig()  { quiereAp = false; quiereCerrar = true; }
bool wifiEnConfig() { return apActivo || quiereAp; }
uint8_t wifiEstadoPrueba() { return prueba; }
void wifiPedirBusqueda() { busPedida = true; }
bool wifiBuscando() { return busPedida || busEnCurso; }

// Búsqueda de redes. Sin conexión, el ESP32 está intentando conectarse todo el tiempo y en ese
// estado el Wi-Fi rechaza la búsqueda: por eso antes no aparecía ninguna red hasta conectarse.
static void busquedaLoop(uint32_t ahora) {
  if (busPedida && prueba != WF_PROBANDO && !pedido) {
    busPedida = false;
    if (WiFi.status() != WL_CONNECTED) {         // pausa los reintentos de conexión
      WiFi.setAutoReconnect(false);
      WiFi.disconnect();
      busPausoSta = true;
    }
    WiFi.scanNetworks(true);                     // en segundo plano
    busEnCurso = true;
    busInicio = ahora;
    busIntentos = 1;
  }
  if (busEnCurso) {
    const int16_t n = WiFi.scanComplete();
    if (n >= 0 || ahora - busInicio > 15000UL) busEnCurso = false;            // terminó (o se dio por terminada)
    else if (n == WIFI_SCAN_FAILED) {
      if (busIntentos < 4) { busIntentos++; WiFi.scanNetworks(true); }       // reintenta (el Wi-Fi puede tardar en quedar libre)
      else busEnCurso = false;
    }
  }
  if (busPausoSta && !busEnCurso && !busPedida) {  // retoma la conexión con la red guardada
    busPausoSta = false;
    WiFi.setAutoReconnect(true);
    if (prueba != WF_PROBANDO && !pedido && WiFi.status() != WL_CONNECTED) WiFi.begin(ssidG, claveG);
  }
}
String wifiRedGuardada() { return String(ssidG); }

bool wifiProbar(const char* ssid, const char* clave) {
  if (prueba == WF_PROBANDO || pedido) return false;
  const size_t ls = strlen(ssid), lc = strlen(clave);
  if (ls < 1 || ls > 32 || lc > 63 || (lc > 0 && lc < 8)) return false;   // WPA exige 8 a 63 caracteres
  snprintf(pSsid, sizeof(pSsid), "%s", ssid);
  snprintf(pClave, sizeof(pClave), "%s", clave);
  pInicio = millis();                     // antes de marcar la prueba: el loop puede mirarla en cualquier momento
  pedido = true;
  prueba = WF_PROBANDO;
  return true;
}

void wifiLoop() {
  const uint32_t ahora = millis();

  if (quiereAp) { quiereAp = false; levantarAp(); }
  if (quiereCerrar && prueba != WF_PROBANDO) {
    quiereCerrar = false;
    if (apActivo) bajarAp();
    if (sinConexion) sinConexion = ahora | 1;   // si sigue sin red, vuelve a ofrecerlo recién en 2 min
  }
  if (apActivo) dns.processNextRequest();
  busquedaLoop(ahora);

  if (pedido) {
    pedido = false;
    pInicio = ahora;
    levantarAp();                         // así se puede ver el resultado aunque se cambie de red
    WiFi.disconnect();
    WiFi.begin(pSsid, pClave);
  }

  const bool con = (WiFi.status() == WL_CONNECTED);

  if (prueba == WF_PROBANDO && ahora - pInicio > 2000) {
    if (con && WiFi.SSID() == String(pSsid)) {
      guardar(pSsid, pClave);
      prueba = WF_OK;
      if ((int32_t)(apCierre - ahora) > 60000) apCierre = ahora + 60000UL;   // deja un minuto para ver el resultado
      Serial.printf("Wi-Fi nueva guardada: %s  IP %s\n", pSsid, WiFi.localIP().toString().c_str());
    } else if (ahora - pInicio > 20000UL) {
      prueba = WF_FALLO;
      WiFi.disconnect();
      WiFi.begin(ssidG, claveG);          // vuelve a la red anterior
      Serial.println("No se pudo conectar a la red nueva; se vuelve a la anterior");
    }
  }

  // Sin ahorro de energía del Wi-Fi: con el "modem sleep" por defecto la placa puede dejar de contestar
  // (ping, páginas) aunque aparezca conectada. Se reaplica cada vez que se reconecta.
  static bool sinSleep = false;
  if (con && !sinSleep) { WiFi.setSleep(false); sinSleep = true; }
  if (!con) sinSleep = false;

  if (con) {
    sinConexion = 0;
    if (!mdnsOk && MDNS.begin(HOSTNAME)) { MDNS.addService("http", "tcp", 80); mdnsOk = true; }
  } else {
    if (sinConexion == 0) sinConexion = ahora | 1;
    if (prueba != WF_PROBANDO && !busPausoSta && ahora - ultReintento > 30000UL) { ultReintento = ahora; WiFi.reconnect(); }
    if (!apActivo && ahora - sinConexion > 120000UL) levantarAp();   // 2 min sin red: ofrece reconfigurar
  }

  if (apActivo && con && prueba != WF_PROBANDO && (int32_t)(ahora - apCierre) >= 0) bajarAp();
}
