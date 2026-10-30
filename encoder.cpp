#include "encoder.h"

// ----------------------------------------------------------------------
//  Decodificador de cuadratura: cuenta cada cambio válido de los pines (código Gray
//  00 -> 01 -> 11 -> 10) y avisa un paso cada ENC_TRANSICIONES cambios. Los saltos
//  imposibles (rebote o lectura perdida) se ignoran y los vaivenes se cancelan.
// ----------------------------------------------------------------------
static const DRAM_ATTR int8_t SALTO[4][4] = {
  /* de 00 */ {  0, -1, +1,  0 },
  /* de 01 */ { +1,  0,  0, -1 },
  /* de 10 */ { -1,  0,  0, +1 },
  /* de 11 */ {  0, +1, -1,  0 },
};

int8_t IRAM_ATTR encoderPaso(EncDecodificador& d, uint8_t pines) {
  pines &= 0x03;
  const int8_t s = SALTO[d.estado & 0x03][pines];
  d.estado = pines;
  if (!s) return 0;
  d.acum += s;
  if (d.acum >= ENC_TRANSICIONES)  { d.acum = 0; return +1; }
  if (d.acum <= -ENC_TRANSICIONES) { d.acum = 0; return -1; }
  return 0;
}

// ----------------------------------------------------------------------
//  Pulsador: antirrebote de 30 ms. Click = se suelta antes de BOTON_LARGO_MS.
//  Pulsación larga = se avisa apenas se cumple el tiempo (sin esperar a soltar).
// ----------------------------------------------------------------------
uint8_t encoderBotonProcesar(EncBoton& b, bool presionado, uint32_t ahora) {
  if (presionado != b.crudo) { b.crudo = presionado; b.cambio = ahora; }
  if (ahora - b.cambio >= 30 && b.crudo != b.estable) {
    b.estable = b.crudo;
    if (b.estable) { b.inicio = ahora; b.largoEnviado = false; }
    else if (!b.largoEnviado) return BTN_CLICK;       // soltó antes del tiempo largo
    return BTN_NADA;
  }
  if (b.estable && !b.largoEnviado && ahora - b.inicio >= BOTON_LARGO_MS) {
    b.largoEnviado = true;
    return BTN_LARGO;
  }
  return BTN_NADA;
}

#if !USAR_ENCODER
void    encoderInit() {}
void    encoderLoop() {}
int16_t encoderGiro() { return 0; }
uint8_t encoderBoton() { return BTN_NADA; }
bool    encoderMantenidoAlArrancar(uint32_t) { return false; }

#else
static EncDecodificador dec;
static EncBoton boton;
static volatile int16_t acum = 0;
static volatile uint8_t pendienteBoton = BTN_NADA;
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

static void IRAM_ATTR isrEncoder() {
  const uint8_t pines = (digitalRead(PIN_ENC_CLK) ? 1 : 0) | (digitalRead(PIN_ENC_DT) ? 2 : 0);
  const int8_t p = encoderPaso(dec, pines);
  if (p) {
    portENTER_CRITICAL_ISR(&mux);
    acum += ENC_INVERTIR ? p : -p;   // con el KY-040 habitual, al girar a la derecha CLK cambia primero (el decodificador lo cuenta como -1)
    portEXIT_CRITICAL_ISR(&mux);
  }
}

void encoderInit() {
  // El KY-040 trae resistencias pull-up propias hacia "+": conectar "+" a 3V3 (nunca a 5V).
  pinMode(PIN_ENC_CLK, INPUT);
  pinMode(PIN_ENC_DT,  INPUT);
  pinMode(PIN_ENC_SW,  INPUT_PULLUP);
  dec.estado = (digitalRead(PIN_ENC_CLK) ? 1 : 0) | (digitalRead(PIN_ENC_DT) ? 2 : 0);
  dec.acum = 0;
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_CLK), isrEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_DT),  isrEncoder, CHANGE);
}

void encoderLoop() {
  const uint8_t r = encoderBotonProcesar(boton, digitalRead(PIN_ENC_SW) == LOW, millis());
  if (r != BTN_NADA) pendienteBoton = r;
}

bool encoderMantenidoAlArrancar(uint32_t ms) {
  const uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    if (digitalRead(PIN_ENC_SW) != LOW) return false;
    delay(20);
  }
  return true;
}

int16_t encoderGiro() {
  portENTER_CRITICAL(&mux);
  const int16_t v = acum;
  acum = 0;
  portEXIT_CRITICAL(&mux);
  return v;
}

uint8_t encoderBoton() {
  const uint8_t r = pendienteBoton;
  pendienteBoton = BTN_NADA;
  return r;
}
#endif
