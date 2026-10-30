#pragma once
#include <Arduino.h>

// Historial en buffers circulares de resolución decreciente:
//   nivel 0: 1 día     (1 punto/min)      guardado en flash
//   nivel 1: 1 semana  (1 punto/5 min)    guardado en flash
//   nivel 2: 1 mes     (1 punto/30 min)   guardado en flash
//   nivel 3: 3 meses   (1 punto/90 min)   guardado en flash
//   nivel 4: 1 hora    (1 punto/10 s)     "en vivo": solo en RAM, se pierde al reiniciar
#define HIST_NIVELES 5

void historialInit();                                        // reserva RAM y carga desde LittleFS
void historialMuestra(float temp, float hum, uint8_t flags); // llamar con cada lectura válida
                                                             // flags: bit0 hum, bit1 deshum, bit2 frío, bit3 ventilador, bit4 otros, bit5 luz
void historialLoop();                                        // volcado periódico a flash
void historialVolcar();                                      // vuelca ya lo pendiente (antes de un reinicio)
void historialJson(uint8_t nivel, Print& out);               // serie lista para Chart.js
size_t historialJsonTam(uint8_t nivel);                      // bytes máximos de esa respuesta
uint16_t historialCantidad(uint8_t nivel);                   // registros guardados en ese nivel (diagnóstico)

// Resumen de la cámara entre dos instantes (epoch UTC). Elige el nivel más fino que todavía
// conserva el dato más viejo pedido. n = 0 si no hay registros en ese intervalo.
struct HistResumen {
  uint16_t n;
  float tProm, tMin, tMax;      // °C
  float hProm, hMin, hMax;      // % HR
  float pHum, pDesh, pFrio;     // % de los registros en que ese relé estuvo activo (aprox.: dentro de cada intervalo cuenta si se encendió alguna vez)
};
HistResumen historialResumen(uint32_t desde, uint32_t hasta);
void historialBorrar();                                      // borra todo el historial (RAM y flash)
