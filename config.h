#pragma once
#include <Arduino.h>

// =====================================================================
//  CONFIGURACIÓN — lo único que tenés que tocar para empezar
// =====================================================================

// ---------- Software (se ve al arrancar, en el menu "Acerca de" y en la web) ----------
#define SOFTWARE_AUTOR    "ForzaDos"
#define SOFTWARE_VERSION  "v1.0.8 OTA"   // cambiala aca cuando hagas una version nueva
// Versión interna del programa (se ve en la web, Diagnóstico). paginas/generar.py la copia a la
// página principal: si el navegador muestra una página vieja guardada, Diagnóstico lo avisa.
#define VERSION_PROGRAMA  "v1.0.8 OTA"

// ---------- Red ----------
#define WIFI_SSID  "TU_RED_WIFI"
#define WIFI_PASS  "TU_CLAVE"
#define HOSTNAME   "camara-curado"   // http://camara-curado.local (si tu celular resuelve mDNS)

// ---------- Acceso a la web ----------
// 1 = la web NO pide usuario ni clave (lo normal en una red de casa).
// 0 = pide usuario y clave (WEB_USUARIO y WEB_CLAVE, mínimo 8 caracteres).
#define WEB_SIN_CLAVE  1
#define WEB_USUARIO  "admin"
#define WEB_CLAVE    "cambiar-esta-clave"   // solo vale con WEB_SIN_CLAVE 0

// ---------- Pines (números GPIO = lo que dice la serigrafía "IOxx") ----------
// Usá siempre el número IOxx. Los nombres D2/D3/D6 de Arduino Uno NO coinciden
// con los GPIO del ESP32 y es la fuente clásica de errores en estas placas.
#define PIN_DHT        27   // Sensor DHT22 (data). Alimentarlo con 3V3.
// Los pines de los 6 relés y qué controla cada uno están en hardware.h.

// Evitar en esta placa: IO0, IO2, IO12, IO15 (arranque), IO6-IO11 / SD0-SD3 / CMD / CLK
// (memoria flash), IO34-IO39 (solo entrada).

// 1 = el módulo de relés enciende con nivel BAJO (lo habitual en módulos optoacoplados)
// 0 = enciende con nivel ALTO
#define RELE_ACTIVO_BAJO  1   // tu módulo: activo en BAJO (con 0 se prenden todos los relés al arrancar)

// ---------- Sensor ----------
#define DHT_TIPO              DHT22
#define INTERVALO_LECTURA_MS  3000UL     // el DHT22 no soporta más de 1 lectura cada 2 s
#define TIMEOUT_SENSOR_MS     300000UL   // 5 min sin lectura válida -> relés a OFF

// ---------- Hora (NTP) ----------
#define GMT_OFFSET_S  (-3 * 3600)        // Argentina, sin horario de verano
#define NTP_SERVER_1  "pool.ntp.org"
#define NTP_SERVER_2  "time.google.com"

// ---------- Historial ----------
#define VOLCADO_MIN  10     // cada cuántos minutos se vuelca el historial a flash
                            // (un corte de luz pierde como máximo estos minutos)

// ---------- Valores por defecto (después se editan desde la web y quedan guardados) ----------
#define DEF_HUM_SP      75.0f   // % HR objetivo
#define DEF_HUM_HIST     3.0f   // histéresis en % HR
#define DEF_TEMP_SP     14.0f   // °C objetivo (refrigeración y alarma)
#define DEF_TEMP_BANDA   3.0f   // ± °C antes de marcar alarma
#define DEF_MIN_ON_S    120     // segundos mínimos encendido (humedad y frío)
#define DEF_MIN_OFF_S   180     // segundos mínimos apagado (protege compresores)
