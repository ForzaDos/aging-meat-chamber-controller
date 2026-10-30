#include "calibracion.h"
#include "control.h"

// Humedad de equilibrio de la sal común (NaCl) saturada, de 5 a 30 °C cada 5 °C (Greenspan 1977).
static const float TABLA[6] = { 75.65f, 75.67f, 75.61f, 75.47f, 75.29f, 75.09f };

static bool     activo = false;
static uint32_t inicio = 0;
static float    ultT = 20.0f, ultH = NAN;

// Promedios por minuto de los últimos CAL_VENTANA minutos (buffer circular).
static float    minProm[CAL_VENTANA];
static uint8_t  minN = 0, minHead = 0;
static double   acum = 0;
static uint32_t acumN = 0, minutoActual = 0;

float calibReferencia(float t) {
  float x = (t - 5.0f) / 5.0f;
  if (x < 0) x = 0;
  if (x > 5) x = 5;
  const int i = (int)x >= 5 ? 4 : (int)x;
  const float f = x - i;
  return TABLA[i] + (TABLA[i + 1] - TABLA[i]) * f;
}

bool calibActivo() {
  if (activo && millis() - inicio >= CAL_MAX_MIN * 60000UL) {   // olvidada: el clima no puede quedar apagado para siempre
    activo = false;
    Serial.println("Calibracion cancelada: supero el tiempo maximo");
  }
  return activo;
}

void calibIniciar() {
  activo = true;
  inicio = millis();
  minN = minHead = 0;
  acum = 0; acumN = 0; minutoActual = 0;
  ultH = NAN;
  Serial.println("Calibracion iniciada (sal comun)");
}

void calibCancelar() {
  if (activo) Serial.println("Calibracion cancelada");
  activo = false;
}

void calibMuestra(float t, float hCruda) {
  if (!activo) return;
  ultT = t;
  ultH = hCruda;
  const uint32_t m = (millis() - inicio) / 60000UL;
  if (m != minutoActual && acumN) {            // cierra el minuto anterior
    minProm[minHead] = (float)(acum / acumN);
    minHead = (minHead + 1) % CAL_VENTANA;
    if (minN < CAL_VENTANA) minN++;
    acum = 0; acumN = 0;
  }
  minutoActual = m;
  acum += hCruda;
  acumN++;
}

CalibEstado calibEstado() {
  CalibEstado e = {};
  e.activo = activo;
  e.ref = calibReferencia(ultT);
  e.actual = ultH;
  e.prom = NAN;
  e.propuesto = 0;
  if (!activo) return e;
  e.minutos = (uint16_t)((millis() - inicio) / 60000UL);
  if (minN) {
    float s = 0, mn = 1e9f, mx = -1e9f;
    for (uint8_t i = 0; i < minN; i++) { const float v = minProm[i]; s += v; if (v < mn) mn = v; if (v > mx) mx = v; }
    e.prom = s / minN;
    e.rango = mx - mn;
    e.propuesto = constrain(roundf((e.ref - e.prom) * 10.0f) / 10.0f, -OFF_H_MAX, OFF_H_MAX);
  }
  e.sePuede = minN > 0 && e.minutos >= CAL_MIN_MIN;
  e.estable = e.sePuede && minN >= CAL_VENTANA && e.rango <= CAL_RANGO_OK;
  return e;
}

bool calibAplicar() {
  const CalibEstado e = calibEstado();
  if (!e.activo || !e.sePuede || isnan(e.prom)) return false;
  cfg.offH = e.propuesto;
  controlGuardarConfig();
  activo = false;
  Serial.printf("Calibracion aplicada: ajuste de humedad %+.1f %%\n", cfg.offH);
  return true;
}
