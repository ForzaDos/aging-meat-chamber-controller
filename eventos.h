#pragma once
#include <Arduino.h>

// Registro de eventos: qué relé se encendió o apagó y cuándo.
// Guarda los últimos EV_MAX eventos en RAM y los vuelca a LittleFS (/eventos.bin).
enum : uint8_t {
  EV_HUM = 0, EV_DESH = 1, EV_FRIO = 2, EV_VENT = 3, EV_LUZ = 4, EV_OTROS = 5,   // relés 1 a 6
  EV_ARRANQUE = 6,   // el equipo arrancó (corte de luz, reinicio o programa nuevo)
  EV_PAUSA    = 7,   // on = control pausado, off = control reanudado
  EV_TEST     = 8,   // on = empezó el test de relés, off = terminó
  EV_PARADA   = 9    // on = parada de emergencia (todo apagado), off = levantada
};

void eventosInit();                                    // después de LittleFS.begin()
void eventosRegistrar(uint8_t cod, bool on, bool manual = false);   // manual = lo forzó una persona (menú o web)
void eventosBorrar();                                  // borra todo el registro (RAM y flash)
void eventosLoop();                                    // pone la hora a los eventos sin hora y vuelca a flash
void eventosVolcar();                                  // vuelca ya lo pendiente (antes de un reinicio)
void eventosJson(Print& out, uint16_t n, uint16_t saltar);  // los n más recientes, salteando "saltar"
