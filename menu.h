#pragma once
#include <Arduino.h>

// Menú del equipo, manejado con el encoder y mostrado en el LCD.
//   Click          -> entra al menú (desde la pantalla normal)
//   Girar          -> recorre las opciones / cambia el valor
//   Click          -> elige / acepta
//   Pulsación larga-> desde la pantalla normal: pausar o reanudar el control (con confirmación)
//                     dentro del menú: salir sin guardar
//   Todo cambio pide confirmación SI/NO (empieza en NO). 20 s sin tocar nada = sale sin guardar.
void menuInit();
void menuLoop();                                              // llamar en cada vuelta del loop()
bool menuActivo();                                            // true mientras el menú ocupa la pantalla
void menuLineas(char l[4][21], uint8_t filas);                // texto a dibujar (filas = 2 o 4)
void menuLineasTest(char l[4][21], uint8_t filas);            // pantalla del test de relés en curso
void menuLineasBalanza(char l[4][21], uint8_t filas);         // pantallas del menú Balanza

// --- Para pruebas ---
void menuEntrada(int16_t giro, uint8_t boton, uint32_t ahora);  // misma lógica que menuLoop() pero con la entrada dada
int  menuRssiFiltrado(int crudo, uint32_t ahora);   // señal Wi-Fi suavizada (dBm, de a 5)
