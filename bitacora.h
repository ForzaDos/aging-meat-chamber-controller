#pragma once
#include <Arduino.h>
#include <FS.h>

// Diario de secado (antes "bitácora"): tandas, piezas y registros diarios.
// Se guarda como líneas JSON en /bitacora.jsonl (solo se agrega al final, nunca se edita en el
// lugar). Cada línea lleva una clave "k"; si una clave se repite, vale la última, y {"k":..,"x":1}
// borra. La página arma el estado leyendo el archivo; la placa solo lo guarda y valida.
//
// Un corte de luz en medio de una escritura nunca daña lo anterior: se agrega una línea por vez, y
// ordenar/restaurar se hace sobre un archivo temporal que recién se activa al terminar.

#define BITACORA_ARCHIVO     "/bitacora.jsonl"   // el nombre interno quedó de cuando se llamaba "bitácora"
#define BITACORA_MAX_BYTES   250000UL   // tope del archivo (aprox. 1250 registros diarios; con la copia .bak y el temporal entra en 1 MB)
#define BITACORA_MAX_LINEA   720        // bytes por línea
#define BITACORA_RESERVA     120000UL   // espacio libre que se deja para el historial
#define BITACORA_MARGEN_BORRAR 20000UL  // margen extra solo para líneas de borrado

void     bitacoraInit();
// Agrega una línea (sin salto de línea). Devuelve 0 si salió bien, o un código HTTP de error:
// 400 = no es JSON válido o le falta "k", 413 = línea muy larga, 507 = no hay lugar.
// Si usarTmp es true se agrega al archivo temporal (para ordenar o restaurar).
int      bitacoraAgregar(const String& linea, bool usarTmp);
bool     bitacoraTmpReiniciar();        // vacía el temporal
bool     bitacoraTmpConfirmar();        // el temporal pasa a ser el diario (el anterior queda como .bak)
size_t   bitacoraTam();                 // bytes usados
void     bitacoraBorrar();              // borra el diario, su copia .bak y el temporal
