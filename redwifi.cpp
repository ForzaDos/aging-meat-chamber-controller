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

// Punto de acceso fijado (BSSID). Si un repetidor comparte el nombre de la red, el ESP32 se conecta al primero que encuentra, no al
// mejor, y puede cambiar en cada reinicio. Fijado, intenta primero ese y, si en 20 s no lo logra, vale cualquiera: nunca se queda
// sin red por esto. Mientras esté en otro, cada 30 min vuelve a probar el fijado.
static uint8_t  bssidFijo[6];
static bool     tieneFijo = false;
static uint32_t intentoFijo = 0;         // millis() del intento con el punto de acceso fijado (0 = no hay uno en curso)
static uint32_t ultRepin = 0;
static volatile uint8_t pendFijoAccion = 0;   // pedido de la web: 1 = fijar pendFijo, 2 = soltar (lo aplica wifiLoop)
static uint8_t  pendFijo[6];

static void cargar() {
  pw.begin("redwifi", false);
  String s = pw.getString("ssid", "");
  String c = pw.getString("clave", "");
  tieneFijo = (pw.getBytes("bssid", bssidFijo, 6) == 6);
  pw.end();
  if (s.length() == 0) { s = WIFI_SSID; c = WIFI_PASS; }
  snprintf(ssidG, sizeof(ssidG), "%s", s.c_str());
  snprintf(claveG, sizeof(claveG), "%s", c.c_str());
}

static void guardar(const char* s, const char* c) {
  const bool otraRed = (strcmp(ssidG, s) != 0);
  pw.begin("redwifi", false);
  if (otraRed) { pw.remove("bssid"); tieneFijo = false; }   // el punto de acceso fijado era de la red anterior
  pw.putString("ssid", s);
  pw.putString("clave", c);
  pw.end();
  snprintf(ssidG, sizeof(ssidG), "%s", s);
  snprintf(claveG, sizeof(claveG), "%s", c);
}

// Arranca una conexión a la red guardada: primero al punto de acceso fijado (si hay), si no a cualquiera.
static void conectar() {
  if (tieneFijo) {
    WiFi.begin(ssidG, claveG, 0, bssidFijo);
    intentoFijo = millis() | 1;
  } else {
    WiFi.begin(ssidG, claveG);
    intentoFijo = 0;
  }
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
  conectar();

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
    if (prueba != WF_PROBANDO && !pedido && WiFi.status() != WL_CONNECTED) conectar();
  }
}
String wifiRedGuardada() { return String(ssidG); }

static String macTexto(const uint8_t* b) {
  char s[18];
  snprintf(s, sizeof(s), "%02X:%02X:%02X:%02X:%02X:%02X", b[0], b[1], b[2], b[3], b[4], b[5]);
  return String(s);
}
String wifiBssidActual() { return WiFi.status() == WL_CONNECTED ? WiFi.BSSIDstr() : String(""); }
String wifiBssidFijo() { return tieneFijo ? macTexto(bssidFijo) : String(""); }
bool wifiFijarBssid(const char* texto) {
  unsigned v[6];
  if (!texto || strlen(texto) != 17) return false;
  if (sscanf(texto, "%2x:%2x:%2x:%2x:%2x:%2x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) return false;
  unsigned o = 0, y = 0xFF;
  for (int i = 0; i < 6; i++) { o |= v[i]; y &= v[i]; }
  if (o == 0 || y == 0xFF) return false;          // 00:00:... y FF:FF:... no son un punto de acceso
  for (int i = 0; i < 6; i++) pendFijo[i] = (uint8_t)v[i];
  pendFijoAccion = 1;
  return true;
}
void wifiQuitarFijo() { pendFijoAccion = 2; }

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

  // Pedido de la web de fijar o soltar el punto de acceso (se aplica acá, no en la tarea de la web).
  if (pendFijoAccion && prueba != WF_PROBANDO && !busEnCurso && !busPedida && !busPausoSta) {
    const uint8_t a = pendFijoAccion;
    pendFijoAccion = 0;
    pw.begin("redwifi", false);
    if (a == 1) { memcpy(bssidFijo, pendFijo, 6); pw.putBytes("bssid", bssidFijo, 6); tieneFijo = true; }
    else { pw.remove("bssid"); tieneFijo = false; }
    pw.end();
    Serial.println(a == 1 ? "Wi-Fi: punto de acceso fijado" : "Wi-Fi: punto de acceso suelto (cualquiera)");
    WiFi.disconnect();
    conectar();
    ultRepin = ahora;
    return;
  }

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
      conectar();                         // vuelve a la red anterior
      Serial.println("No se pudo conectar a la red nueva; se vuelve a la anterior");
    }
  }

  // Sin ahorro de energía del Wi-Fi: con el "modem sleep" por defecto la placa puede dejar de contestar
  // (ping, páginas) aunque aparezca conectada. Se reaplica cada vez que se reconecta.
  static bool sinSleep = false;
  if (con && !sinSleep) { WiFi.setSleep(false); sinSleep = true; }
  if (!con) sinSleep = false;

  // Punto de acceso fijado: si conectó, listo; si en 20 s no, se conecta a cualquiera (y cada 30 min vuelve a probar el fijado).
  if (intentoFijo) {
    if (con) intentoFijo = 0;
    else if (ahora - intentoFijo > 20000UL && prueba != WF_PROBANDO && !busPausoSta) {
      intentoFijo = 0;
      WiFi.disconnect();
      WiFi.begin(ssidG, claveG);
      ultRepin = ahora;
      Serial.println("Wi-Fi: el punto de acceso fijado no responde; se conecta a cualquiera");
    }
  } else if (con && tieneFijo && prueba != WF_PROBANDO && !apActivo && !busEnCurso && !busPedida && ahora - ultRepin > 1800000UL) {
    const uint8_t* b = WiFi.BSSID();
    ultRepin = ahora;
    if (b && memcmp(b, bssidFijo, 6) != 0) {
      Serial.println("Wi-Fi: conectada a otro punto de acceso; se prueba de nuevo el fijado");
      WiFi.disconnect();
      conectar();
    }
  }

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
