#pragma once
#include <Arduino.h>

// =====================================================================
//  HARDWARE: módulo de 6 relés, encoder y pantalla LCD.
//  (El sensor y el tipo de módulo de relés están en config.h)
// =====================================================================

// ---------- Módulo de 6 relés ----------
// 1) Cableado ESP32 -> módulo (cambiar solo si se mueven los cables de IN1..IN6):
#define PIN_IN1         26
#define PIN_IN2         25
#define PIN_IN3         13
#define PIN_IN4         23
#define PIN_IN5         19
#define PIN_IN6         18
// 2) Qué controla cada relé. Para reasignar funciones sin tocar los cables de la placa,
//    alcanza con cambiar estas líneas (y conectar cada equipo de 220 V a su relé).
#define PIN_RELE_FRIO   PIN_IN1   // relé 1 = refrigeración (heladera / compresor)
#define PIN_RELE_HUM    PIN_IN2   // relé 2 = humidificador
#define PIN_RELE_DESH   PIN_IN3   // relé 3 = deshumidificador
#define PIN_RELE_VENT   PIN_IN4   // relé 4 = ventilador (con programa horario desde la web)
#define PIN_RELE_LUZ    PIN_IN5   // relé 5 = AUX1 (el nombre se cambia desde la web)
#define PIN_RELE_OTROS  PIN_IN6   // relé 6 = AUX2 (el nombre se cambia desde la web)

// Número de relé (1..6) que corresponde a un pin; 0 si no es ninguno.
inline uint8_t numeroRele(uint8_t pin) {
  const uint8_t in[6] = { PIN_IN1, PIN_IN2, PIN_IN3, PIN_IN4, PIN_IN5, PIN_IN6 };
  for (uint8_t i = 0; i < 6; i++) if (in[i] == pin) return i + 1;
  return 0;
}

// ---------- Encoder rotativo con pulsador (KY-040) ----------
// 0 = sin encoder   1 = con encoder (menú en el LCD)
#define USAR_ENCODER    1
// CLK y DT van a pines de solo entrada (IO34 / IO35): el módulo KY-040 trae sus propias
// resistencias pull-up, así que su pin "+" va a 3V3 (NUNCA a 5V). SW usa el pull-up interno.
#define PIN_ENC_CLK     34
#define PIN_ENC_DT      35
#define PIN_ENC_SW       4
// Si al girar a la derecha el menú se mueve para el lado contrario, poné 1 (o cruzá CLK y DT).
#define ENC_INVERTIR     0
// Cambios de pines por cada click del eje: 2 es lo habitual. Si hacen falta 2 clicks por paso, poné 1; si un click da 2 pasos, poné 4.
#define ENC_TRANSICIONES 2

// ---------- Balanzas por Wi-Fi (proyectos "balanza" y "balanza_c3_10kg") ----------
// 1 = busca las balanzas en la red de la casa (BALANZA_HOST.local), lee el peso y sigue la merma de
//     la pieza testigo de CADA una (menú Balanza del LCD, pestaña Balanza de la web y Diario de secado).
// 0 = sin balanza.
// (Se probó por Bluetooth, pero a esta placa no le alcanza la memoria para Bluetooth + Wi-Fi + web.)
//
// Tabla de balanzas: índice 0 es la histórica (la de 1 kg, NVS "balanzaRed"/"pesaje" y archivo
// /pesaje.bin, así no se pierde nada de lo ya registrado). Las que siguen usan "balanzaRed1"/
// "pesaje1" y /pesaje1.bin, etc. El orden manda en la web y en el menú del LCD.
#define USAR_BALANZA     1
#define BALANZAS_MAX     2
#define BALANZA_HOST     "balanza"         // nombre de la balanza en la red (HOSTNAME de balanza/config.h)
// host = nombre en la red; alt = segundo nombre por si mDNS le agregó un sufijo ("balanza10k-2"):
// pasa cuando dos placas publican el mismo nombre y la segunda se renombra sola.
// exigeId: la placa tiene que decir su nombre ("host" en /api/peso); si contesta otra, o ninguna, no se la usa.
struct BalanzaDef { const char* host; const char* alt; const char* nombre; float capacidad; bool exigeId; };
static const BalanzaDef BALANZAS[BALANZAS_MAX] = {
  { BALANZA_HOST, nullptr,        "1 kg",  1100.0f, false },   // celda de 1 kg (tope 1,1 kg, como siempre)
  { "balanza10k", "balanza10k-2", "10 kg", 11000.0f, true }, // celda de 10 kg (proyecto balanza_c3_10kg)
};

// ---------- Pantalla I2C ----------
//   0 = sin pantalla
//   1 = LCD 20x4 con módulo I2C     (librería "hd44780")
//   2 = LCD 16x2 con módulo I2C     (librería "hd44780")
#define PANTALLA_TIPO   1
#define PIN_SDA         21
#define PIN_SCL         22

// ---------- Rangos de los objetivos ----------
// Valen para el menú del equipo y para los deslizables de la web (si los cambiás acá,
// cambiá también min/max de los deslizables en pagina.h).
#define HUM_SP_MIN      40.0f
#define HUM_SP_MAX      100.0f
#define TEMP_SP_MIN      5.0f
#define TEMP_SP_MAX     30.0f

// ---------- Valores por defecto de lo nuevo ----------
#define DEF_TEMP_HIST          1.0f    // histéresis superior de temperatura (°C)
#define DEF_TEMP_HIST_INF      0.0f    // histéresis inferior de temperatura (°C): 0 = el frío apaga en el objetivo
// Rangos de las histéresis (los mismos en control.cpp, web.cpp, menu.cpp y los deslizables de la página).
#define HIST_HUM_MIN   0.5f
#define HIST_HUM_MAX  10.0f
#define HIST_TEMP_MIN  0.5f
#define HIST_TEMP_MAX  5.0f
#define DEF_MANUAL_MIN         15      // minutos que dura un forzado manual antes de volver a automático (5 a 120)
#define DEF_VENT_ACTIVO        false   // el programa del ventilador arranca apagado
#define DEF_VENT_PERIODO_H     1       // se repite cada 1 h
#define DEF_VENT_DURACION_MIN  5       // enciende 5 minutos por ciclo
#define DEF_VENT_ANCLA_MIN     0       // el ciclo arranca a las 00:00
#define VENT_DURACION_MAX      180     // minutos (límite del deslizable)

// ---------- Modos de test avanzados (Diagnóstico) ----------
// 1 = además del test "de a uno", aparecen "Sumar 1 a 6" y "Todos juntos" (para probar la fuente con
//     varios relés encendidos). Pensado para el armado y el servicio técnico.
// 0 = solo el test de a uno (versión para el usuario final).
#define TEST_AVANZADO    0

// ---------- Protecciones fijas ----------
// No se pueden cambiar desde la web ni desde el menú: solo acá, recompilando.
// El compresor espera al menos esto apagado antes de volver a arrancar, aunque "Mínimo apagado" sea menor.
// También vale para el test de relés: no arranca si la heladera no estuvo apagada este tiempo.
#define FRIO_MIN_OFF_S   180
// Límites absolutos con la lectura del sensor SIN el ajuste de calibración. Mandan sobre la
// pausa y los forzados manuales (no sobre la calibración: el sensor está en el frasco).
#define SEG_T_MAX        32.0f   // °C: arriba de esto el frío se enciende siempre...
#define SEG_T_HIST        2.0f   // ...hasta bajar esto por debajo
#define SEG_T_MIN         1.0f   // °C: abajo de esto el frío se apaga siempre
#define SEG_HR_MAX       98.0f   // % HR: arriba de esto el humidificador se apaga siempre
#define SEG_HR_MIN       35.0f   // % HR: abajo de esto el deshumidificador se apaga siempre
