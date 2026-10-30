#pragma once
#include <Arduino.h>

// =====================================================================
//  CALIBRACIÓN DEL SENSOR (DHT22)
//
//  1) Ajuste fijo (offset): cfg.offT y cfg.offH se suman a cada lectura.
//     Se cambian desde la web o el menú del equipo.
//
//  2) Asistente con sal común saturada: el sensor se deja en un frasco cerrado con
//     sal húmeda (pasta de sal con un poco de agua, que queden cristales sin
//     disolver). Una solución saturada de cloruro de sodio mantiene ~75,5 % HR casi
//     sin depender de la temperatura. El asistente promedia la lectura SIN corregir y,
//     cuando está estable, propone el ajuste de humedad:
//         ajuste = humedad de referencia - promedio medido
//     Mientras dura, el control de clima queda apagado (el sensor no está en la
//     cámara) y no se guarda historial.
//  Referencia: Greenspan (1977), interpolada según la temperatura.
// =====================================================================

#define OFF_T_MAX       5.0f     // ± °C
#define OFF_H_MAX      15.0f     // ± % HR
#define CAL_MIN_MIN     60       // minutos mínimos antes de poder aplicar
#define CAL_VENTANA     30       // minutos que se promedian
#define CAL_RANGO_OK    0.6f     // % HR: si los promedios por minuto de la ventana varían menos, está estable
#define CAL_MAX_MIN     1440     // a las 24 h se cancela sola y el clima vuelve a funcionar

struct CalibEstado {
  bool     activo;
  float    ref;        // % HR de referencia a la temperatura actual
  float    actual;     // última humedad leída, sin corregir
  float    prom;       // promedio sin corregir de la ventana (NAN si todavía no hay)
  float    rango;      // máx - mín de los promedios por minuto de la ventana
  uint16_t minutos;    // minutos desde que empezó
  bool     estable;
  bool     sePuede;    // ya pasaron CAL_MIN_MIN minutos
  float    propuesto;  // ajuste de humedad que se aplicaría (limitado a ± OFF_H_MAX)
};

void        calibIniciar();
void        calibCancelar();
bool        calibAplicar();                             // guarda cfg.offH; false si todavía no hay datos suficientes
bool        calibActivo();
void        calibMuestra(float tempCorregida, float humCruda);   // en cada lectura válida
CalibEstado calibEstado();
float       calibReferencia(float temp);                // % HR de la sal común saturada a esa temperatura
