#pragma once
#include <Arduino.h>
#include "hardware.h"

// =====================================================================
//  Seguimiento de la pieza testigo colgada en la balanza (por Wi-Fi).
//  - Peso inicial, merma objetivo y la tanda del Diario a la que pertenece (NVS "pesaje").
//    La pieza de la balanza es SIEMPRE la testigo: su merma es la de la tanda.
//  - Registro del peso: un punto cada PESAJE_INTERVALO_S (promedio del intervalo) en
//    /pesaje.bin, que se vacía al empezar un seguimiento nuevo. Necesita la hora de red (NTP).
//  - Estimación: ritmo de merma de las últimas PESAJE_VENTANA_H horas (los primeros días, desde
//    el inicio) y los días que faltan para la merma objetivo a ese ritmo.
// =====================================================================

#define PESAJE_INTERVALO_S   1800UL     // 30 min por punto
#define PESAJE_MAX_PUNTOS    8000       // ~166 días (64 KB en la memoria de la placa)
#define PESAJE_VENTANA_H     72         // horas para calcular el ritmo de merma
#define PESAJE_MIN_H         12         // con menos historia no se estima
#define PESAJE_DESCOLGADA    0.15f      // menos de esta fracción del peso inicial = la pieza no está colgada

struct PesajeEstado {
  bool     activo;          // hay un seguimiento en curso
  bool     pausado;         // en pausa (pieza descolgada para limpiar o revisar): no se registra
  uint32_t pausaDesde;      // epoch UTC del comienzo de la pausa (0 si no está en pausa)
  bool     descolgada;      // la balanza marca casi nada: no se registra (¿se olvidó de pausar?)
  float    p0;              // peso inicial (g)
  float    objetivo;        // merma objetivo (%)
  uint32_t inicio;          // epoch UTC
  char     tanda[25];       // clave de la tanda en el Diario ("" = sin vincular)
  char     nombre[41];      // nombre de la tanda (para el LCD y la web)
  float    peso;            // último peso conocido (g), NAN si no hay
  bool     enVivo;          // peso viene de la balanza ahora mismo (si no, es el último registrado)
  float    merma;           // % (NAN si no hay peso)
  float    dias;            // días desde el inicio
  float    ritmo;           // % por día (NAN si todavía no se puede estimar)
  float    falta;           // días hasta la merma objetivo (NAN si no se puede estimar; 0 si ya llegó)
  uint16_t puntos;          // puntos registrados
};

#if USAR_BALANZA
void         pesajeInit();              // en setup(), después de LittleFS.begin()
void         pesajeLoop();              // en cada vuelta del loop()
PesajeEstado pesajeEstado();            // se puede llamar desde cualquier tarea
// Empieza un seguimiento nuevo (borra el registro anterior). inicio = 0 -> ahora.
// false si los datos no sirven (p0 de 1 a 1100 g, objetivo de 1 a 80 %) o no hay hora de red ni inicio.
bool         pesajeIniciar(float p0, float objetivo, uint32_t inicio, const char* tanda, const char* nombre);
// Cambia datos del seguimiento en curso sin borrar el registro (NAN / nullptr = no cambiar).
bool         pesajeEditar(float p0, float objetivo, const char* tanda, const char* nombre);
void         pesajeTerminar();          // deja de registrar (el registro queda para verlo)
// Pausa / retoma el seguimiento en curso (queda guardado). La merma queda en el último peso
// registrado y el ritmo se sigue calculando por fecha real. false si no hay seguimiento.
bool         pesajePausar(bool pausar);
// Registro como JSON: {"p0":..,"obj":..,"ini":..,"pts":[[epoch,gramos],...]} (como mucho ~600 puntos).
void         pesajeJson(Print& out);
size_t       pesajeJsonTam();
#endif
