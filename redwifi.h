#pragma once
#include <Arduino.h>

// =====================================================================
//  Red Wi-Fi configurable sin recompilar
//  - La red y la clave se guardan en la memoria permanente de la placa.
//    Si no hay ninguna guardada se usan WIFI_SSID / WIFI_PASS de config.h.
//  - "Modo configuración": la placa crea su propia red (WIFI_AP_NOMBRE, sin clave) y en
//    http://192.168.4.1/wifi se elige la red de la casa. Arranca cuando:
//      * se mantiene apretado el encoder al encender,
//      * no hay red configurada (sigue con el texto de ejemplo de config.h),
//      * pasan 2 minutos sin conexión, o
//      * se pide desde el menú del equipo (Diagnostico > Config WiFi).
//  - Al probar una red nueva, si no conecta en 20 s se vuelve a la anterior.
//  (El archivo no se llama wifi.h para no confundirse con WiFi.h del IDE.)
// =====================================================================
#define WIFI_AP_NOMBRE   "CamaraCurado-Setup"
#define WIFI_AP_IP       "192.168.4.1"

enum : uint8_t { WF_SIN_PRUEBA = 0, WF_PROBANDO = 1, WF_OK = 2, WF_FALLO = 3 };

void     wifiInit(bool forzarConfig);        // en setup(): conecta (espera hasta 15 s)
void     wifiLoop();                         // en cada vuelta del loop()
void     wifiIniciarConfig();                // pide el modo configuración (se levanta en wifiLoop)
void     wifiCerrarConfig();                 // pide cerrarlo (no se cierra en medio de una prueba de red)
bool     wifiEnConfig();                     // true si el modo configuración está activo o pedido
bool     wifiProbar(const char* ssid, const char* clave);   // false si los datos no sirven o ya hay una prueba
uint8_t  wifiEstadoPrueba();                 // WF_*
String   wifiRedGuardada();                  // nombre de la red guardada
// Búsqueda de redes (página /wifi). La hace wifiLoop(): si la placa no está conectada, primero
// detiene los reintentos de conexión (mientras intenta conectarse, el Wi-Fi no puede buscar redes)
// y los retoma al terminar. Mientras wifiBuscando() es true, los resultados todavía no están.
void     wifiPedirBusqueda();
bool     wifiBuscando();
