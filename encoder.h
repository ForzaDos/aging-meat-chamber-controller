#pragma once
#include <Arduino.h>
#include "hardware.h"

// =====================================================================
//  Encoder rotativo con pulsador (KY-040 y similares)
//  Pines, sentido de giro y transiciones por click: en hardware.h.
// =====================================================================
#define BOTON_LARGO_MS  1500UL   // mantener apretado este tiempo = pulsación larga

enum : uint8_t { BTN_NADA = 0, BTN_CLICK = 1, BTN_LARGO = 2 };

void    encoderInit();
void    encoderLoop();            // llamar en cada vuelta del loop(): lee el pulsador
int16_t encoderGiro();            // pasos (detents) girados desde la última llamada: + derecha, - izquierda
uint8_t encoderBoton();           // BTN_CLICK / BTN_LARGO una sola vez por pulsación
bool    encoderMantenidoAlArrancar(uint32_t ms);   // true si el pulsador sigue apretado durante "ms" (se usa solo en setup())

// --- Piezas puras (se usan internamente; expuestas para poder probarlas) ---
// Decodificador en cuadratura: se le da el estado de los pines (bit0 = CLK, bit1 = DT)
// cada vez que alguno cambia. Cuenta cada transición válida y devuelve +1 / -1 cada
// ENC_TRANSICIONES transiciones (un "click" del eje); descarta el rebote.
struct EncDecodificador { uint8_t estado = 3; int8_t acum = 0; };
int8_t  encoderPaso(EncDecodificador& d, uint8_t pines);
// Pulsador con antirrebote: presionado = true mientras está apretado.
struct EncBoton { bool estable = false; bool crudo = false; uint32_t cambio = 0, inicio = 0; bool largoEnviado = false; };
uint8_t encoderBotonProcesar(EncBoton& b, bool presionado, uint32_t ahora);
