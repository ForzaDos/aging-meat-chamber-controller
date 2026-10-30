#pragma once
#include <Arduino.h>
#include <LittleFS.h>

// Guarda cabecera + datos en un archivo temporal y después lo renombra: si se corta la luz
// en medio de la escritura, el archivo anterior queda intacto. Lo usan el historial y el registro.
inline bool archivoGuardarSeguro(const char* nombre, const void* cab, size_t nCab, const void* datos, size_t nDatos) {
  char tmp[32];
  snprintf(tmp, sizeof(tmp), "%s.tmp", nombre);
  File f = LittleFS.open(tmp, "w");
  if (!f) return false;
  size_t w = f.write((const uint8_t*)cab, nCab);
  w += f.write((const uint8_t*)datos, nDatos);
  f.close();
  if (w != nCab + nDatos) { LittleFS.remove(tmp); return false; }
  if (!LittleFS.rename(tmp, nombre)) {
    LittleFS.remove(nombre);
    return LittleFS.rename(tmp, nombre);
  }
  return true;
}

// Si un corte de luz cayó justo entre borrar el archivo y renombrar el temporal, queda solo
// el ".tmp" (completo): se lo recupera antes de cargar.
inline void archivoRecuperar(const char* nombre) {
  char tmp[32];
  snprintf(tmp, sizeof(tmp), "%s.tmp", nombre);
  if (!LittleFS.exists(nombre) && LittleFS.exists(tmp)) LittleFS.rename(tmp, nombre);
}
