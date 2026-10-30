#pragma once
#include <Arduino.h>
#include "hardware.h"

// =====================================================================
//  Balanza por Wi-Fi. La balanza es otra placa (proyecto "balanza") con la celda de carga, en la
//  misma red de la casa. Esta placa la busca por su nombre (BALANZA_HOST.local, hardware.h),
//  le pide el peso una vez por segundo (GET /api/peso) y le manda tarar / calibrar (POST /api/orden).
//  Todo corre en una tarea propia: una balanza que no contesta no frena el control de clima.
//
//  Vinculación: la dirección IP encontrada se guarda y se usa directo. Si deja de contestar se vuelve
//  a buscar por nombre (por si el router le cambió la IP). "Olvidar" borra la IP guardada.
// =====================================================================

// Banderas del peso (las arma esta placa con lo que contesta la balanza).
enum : uint8_t {
  BAL_ESTABLE    = 0x01,   // el peso no se mueve
  BAL_SOBRECARGA = 0x02,   // más de lo que soporta la celda
  BAL_SIN_HX711  = 0x04,   // la balanza no lee la celda (cables)
  BAL_SIN_CALIB  = 0x08,   // nunca se calibró: el peso es aproximado
  BAL_OCUPADA    = 0x10,   // tarando o calibrando (unos 2 s)
};

enum : uint8_t { CON_APAGADO = 0, CON_BUSCANDO = 1, CON_SIN_RESPUESTA = 2, CON_CONECTADA = 3 };

struct BalanzaEstado {
  bool     conectada;      // la balanza contesta y hay datos recientes
  bool     hayPeso;        // gramos es válido (conectada y el HX711 responde)
  float    gramos;
  uint8_t  flags;          // BAL_*
  int      rssi;           // señal Wi-Fi de la balanza (dBm), 0 si no se sabe
  uint8_t  estadoCon;      // CON_*
  bool     activo;         // conexión habilitada (se apaga desde el menú o la web)
  char     vinculada[18];  // IP de la balanza ("" = todavía no se encontró)
  char     respuesta[48];  // respuesta a la última orden ("OK T", "ERR ...", vacío = todavía no llegó)

  // Temperatura: esta placa le manda su temperatura en cada consulta y la balanza compensa la deriva
  // de la celda (ver balanza/config.h). NAN = no se sabe.
  float    tCelda;         // temperatura estimada de la celda (°C)
  float    corr;           // gramos que la balanza está corrigiendo ahora
  bool     comp;           // compensación encendida
  float    kCero, kSpan;   // deriva del cero (g/°C) y de la sensibilidad (%/°C)
  float    tTara, tCal;    // temperatura a la que se taró / calibró
  uint8_t  avisoNivel;     // 0 nada, 1 información, 2 atención
  char     aviso[100];
  bool     aprendiendo;    // aprendiendo la deriva (peso fijo, sin tocar)
  uint16_t aprN;           // muestras (1 por minuto)
  float    aprDt, aprPend, aprR2, aprCarga;   // rango de T (°C), deriva medida (g/°C), ajuste, carga (g)
};

#if USAR_BALANZA
void          balanzaInit();                  // en setup(), después de wifiInit()
BalanzaEstado balanzaEstado();                // copia del estado (se puede llamar desde cualquier tarea)
// Envía una orden ("T" tarar, "C500" calibrar con 500 g, "R" borrar calibración, "A"/"AF"/"AX"
// aprender la deriva / terminar / cancelar, "M1"/"M0" compensación sí/no). t = temperatura de la
// tara o calibración (NAN = la que estima la balanza con los datos de esta placa). false si la
// balanza no está conectada o todavía hay otra orden sin enviar.
bool          balanzaOrden(const char* orden, float t = NAN);
void          balanzaActivar(bool on);        // habilita / deshabilita la conexión (queda guardado)
void          balanzaReconectar();            // vuelve a buscar la balanza por nombre
void          balanzaOlvidar();               // borra la IP guardada (y la busca de nuevo)
#endif
