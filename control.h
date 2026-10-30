#pragma once
#include <Arduino.h>
#include "hardware.h"

// Límites de la duración del forzado manual (el valor por defecto, DEF_MANUAL_MIN, está en hardware.h).
#define MANUAL_MIN_MIN    5
#define MANUAL_MIN_MAX  120

// Relés de clima que se pueden forzar a mano (índices de est.manual[]).
enum : uint8_t { R_HUM = 0, R_DESH = 1, R_FRIO = 2 };
// Modos de cada uno.
enum : uint8_t { MAN_AUTO = 0, MAN_ON = 1, MAN_OFF = 2 };

struct Config {
  float    humSP;           // % HR objetivo
  float    humHist;         // histéresis de humedad (% HR)
  float    tempSP;          // °C objetivo
  float    tempHist;        // histéresis de temperatura (°C)
  float    tempBanda;       // ± °C tolerados antes de mostrar alarma
  uint16_t minOnS;          // segundos mínimos encendido del humidificador y el deshumidificador
  uint16_t minOnTempS;      // segundos mínimos encendido de la refrigeración
  uint16_t minOffS;         // segundos mínimos apagado (los tres relés de clima; solo se cambia desde la web)
  bool     ventActivo;      // habilita el programa del ventilador
  uint8_t  ventPeriodoH;    // se repite cada N horas: 1, 2, 3, 4, 6, 8, 12 o 24
  uint16_t ventDuracionMin; // minutos encendido en cada ciclo
  uint16_t ventAnclaMin;    // minuto del día (0..1439) en que arranca el primer ciclo
  bool     activo;          // false = control en pausa (AUX1 y AUX2 no se ven afectados)
  uint16_t manualMin;       // minutos que dura un forzado manual (5..120) antes de volver a automático
  char     nomAux1[13];     // nombre del relé 5 (AUX1), se cambia desde la web
  char     nomAux2[13];     // nombre del relé 6 (AUX2)
  float    offT;            // ajuste de calibración de temperatura (°C, se suma a la lectura)
  float    offH;            // ajuste de calibración de humedad (% HR, se suma a la lectura)
};

struct Estado {
  float    temp = NAN;
  float    hum  = NAN;
  float    tempCruda = NAN;     // lectura del sensor sin el ajuste de calibración
  float    humCruda  = NAN;
  bool     releHum   = false;
  bool     releDesh  = false;
  bool     releFrio  = false;
  bool     releVent  = false;
  bool     releLuz   = false;
  bool     releOtros = false;
  bool     alarmaTemp = false;
  bool     sensorOk   = false;
  int32_t  ventSeg    = -1;     // ventilador: encendido = segundos que le quedan; apagado = segundos al próximo encendido; -1 = no aplica
  uint8_t  manual[3]    = {0, 0, 0};   // por relé de clima (hum, desh, frío): MAN_AUTO / MAN_ON / MAN_OFF
  int32_t  manualSeg[3] = {0, 0, 0};   // segundos que faltan para que vuelva a automático
};

extern Config cfg;
extern Estado est;

void    controlInit();                                   // llamar PRIMERO en setup()

// La web corre en otra tarea (y a veces en el otro núcleo) que el loop(). Todo lo que cambia
// cfg o est desde la web, y el loop() mientras controla, se hace con este candado tomado,
// así nunca se pisan a mitad de un cambio. Uso:  { ControlCandado c;  ...  }
void    controlBloquear();
void    controlLiberar();
struct ControlCandado { ControlCandado() { controlBloquear(); } ~ControlCandado() { controlLiberar(); } };
void    controlGuardarConfig();                          // persiste cfg en NVS
void    controlActualizar(float t, float h, bool ok);    // una vez por lectura del sensor
void    controlSetLuz(bool on);                          // relé 5 = AUX1 (antes "luz"), manual
void    controlSetOtros(bool on);                        // relé 6 = AUX2 (antes "otros"), manual
// Cambia el nombre de AUX1 (cual = 0) o AUX2 (cual = 1). Solo letras sin acentos, números,
// espacio y - . / ( ) + #; máximo 12; vacío = nombre por defecto. No guarda: llamar a controlGuardarConfig().
void    controlNombreAux(uint8_t cual, const char* nombre);
// Fuerza un relé de clima (R_HUM, R_DESH, R_FRIO) a MAN_AUTO / MAN_ON / MAN_OFF.
// Devuelve 0 si se aplicó, FORZAR_RECHAZADO si no se hizo nada (test, actualización, calibración
// parada de emergencia o límite de seguridad: el motivo en controlForzarMotivo()), o los segundos que hay que esperar si
// el encendido todavía no se permite (tiempo mínimo apagado). Apagar es inmediato. Funciona también en pausa.
// Encender el humidificador apaga el deshumidificador y viceversa (nunca los dos a la vez).
#define FORZAR_RECHAZADO 0xFFFFFFFFu
uint32_t controlForzar(uint8_t cual, uint8_t modo);
const char* controlForzarMotivo();   // "test" | "ota" | "calibracion" | "seguridad" | "parada" (del último rechazo)
// Actualización por Wi-Fi: true = clima y ventilador apagados y automático suspendido mientras se graba
// (vuelve solo a los 5 min si la carga se cuelga). Desde la web, con ControlCandado.
void     controlParada(bool on);                         // PARADA DE EMERGENCIA: todo apagado y control suspendido (se guarda en NVS)
bool     controlParadaActiva();
void     controlOta(bool on);
bool     controlOtaActiva();
void     controlManualLoop();                            // llamar en cada vuelta del loop(): vence los forzados
uint8_t controlPeriodoValido(int horas);                 // ajusta al período permitido más cercano (1,2,3,4,6,8,12,24)
extern const uint8_t PERIODOS_VENT[8];                   // esos períodos, en horas

// ---------------------------------------------------------------------
//  MODO TEST DE RELÉS
//  Prende los 6 relés de a uno (nunca dos a la vez): TEST_ON_MS encendido y
//  TEST_PAUSA_MS apagado entre uno y otro. Mientras dura, el control automático,
//  el ventilador y los forzados manuales quedan suspendidos. Al terminar (o al
//  detenerlo) todo queda apagado, AUX1 y AUX2 vuelven a como estaban y el
//  clima arranca contando el tiempo mínimo apagado (protege el compresor).
//  ¡Hacerlo con los 220 V de todos los equipos desconectados!
// ---------------------------------------------------------------------
//  Modos avanzados (si TEST_AVANZADO = 1 en hardware.h), para probar la fuente con carga:
//    TEST_SUMA : prende el relé 1, a los TEST_SUMA_MS suma el 2, y así hasta tener los 6.
//    TEST_TODOS: prende los 6 a la vez.
//    Los dos quedan encendidos hasta que se detienen o como máximo TEST_LARGO_MS.
// ---------------------------------------------------------------------
#define TEST_ON_MS      3000UL
#define TEST_PAUSA_MS   1000UL
#define TEST_RELES      6
#define TEST_SUMA_MS    3000UL
#define TEST_LARGO_MS  120000UL
enum : uint8_t { TEST_NINGUNO = 0, TEST_UNO = 1, TEST_SUMA = 2, TEST_TODOS = 3 };
// TEST_UNO / TEST_SUMA / TEST_TODOS; TEST_NINGUNO = detener. Lo ejecuta controlTestLoop(); desde la web, llamar con ControlCandado.
// Devuelve 0 si se aceptó, o los segundos que faltan para que la heladera lleve FRIO_MIN_OFF_S apagada.
uint32_t    controlTestPedirModo(uint8_t modo);
void        controlTestLoop();                // llamar en cada vuelta del loop()
bool        controlTestActivo();
uint8_t     controlTestModo();                // modo en curso (TEST_NINGUNO si no hay test)
uint8_t     controlTestMascara();             // bit i = función i encendida por el test
int8_t      controlTestRele();                // función encendida si hay una sola (0 hum, 1 desh, 2 frío, 3 vent, 4 luz, 5 otros); -1 = ninguna o varias
uint8_t     controlTestNumero();              // de a uno: 1..6, relé en curso o el próximo; sumar/todos: cuántos hay encendidos
uint8_t     controlTestHechos();              // de a uno: relés que ya terminaron su turno; sumar/todos: cuántos hay encendidos
uint8_t     controlTestOrden(uint8_t k);      // función conectada al relé k+1
uint16_t    controlTestSegRestantes();        // segundos que faltan para terminar
const char* controlNombreRele(uint8_t i);     // nombre de la función 0..5
