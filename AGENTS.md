# AGENTS.md — Cámara de curado

Sketch Arduino para ESP32. **No** es PlatformIO: no hay `platformio.ini`, `package.json`, tests ni CI.
Se compila y sube con `subir.mjs`. Contexto funcional/hardware en `CLAUDE.md`; versiones y pendientes
en `plan.md`. Leer ambos antes de tocar `control.cpp` o `web.cpp`.

## Comandos

- **Si editaste cualquier `.html` de `paginas/`** → primero `python paginas/generar.py`.
  Regenera `pagina.h`, `pagina_bitacora.h` y `pagina_wifi.h` (gzip + inyecta `VERSION_PROGRAMA`).
  Si lo salteás, la placa sigue sirviendo la página vieja y los `.h` quedan desincronizados.
- Compilar + subir por OTA: `node subir.mjs` (destino por defecto `192.168.18.88`).
- Compilar + subir por USB: `node subir.mjs COM3`.
- La web no pide clave (`WEB_SIN_CLAVE 1` en `config.h`).
- `subir.mjs` usa un `arduino-cli.exe` **hardcodeado** en `C:/Program Files/Arduino IDE/...`.
  Si el IDE está en otra ruta, ajustar `CLI` (línea 14).
- Monitor serie: **115200** baudios.

## Reglas que rompen el equipo si se ignoran

- **Partición `custom` (`partitions.csv`) siempre.** Cambiar de esquema (ej. "No OTA") borra
  historial, diario, eventos y pesajes en LittleFS. `subir.mjs` ya pasa `PartitionScheme=custom`.
- El binario ocupa ~86 % del slot de 1,44 MB (`0x170000`). Medir antes de agregar features grandes.
- `SOFTWARE_VERSION` y `VERSION_PROGRAMA` (`config.h`) se cambian **juntas**: `generar.py` copia la
  segunda a la página y la web avisa si el programa y la página no coinciden.
- Toda escritura HTTP es POST y **requiere el header `X-Camara`** (CSRF), además del Digest.
  Lo aplica un middleware (`web.cpp:57`), pero los body handlers corren **antes** de los middlewares:
  el handler de OTA valida a mano (`web.cpp:617`). Cualquier upload nuevo debe replicar esa validación.
- `est` / `cfg` solo se tocan con el candado: `ControlCandado` (RAII) o `controlBloquear()` /
  `controlLiberar()`. `loop()` lo mantiene tomado mientras controla.
- Los `*.bin` de LittleFS tienen formato fijo (`historial.cpp`, `eventos.cpp`, `pesaje.cpp`) y
  `bitacora.jsonl` es append-only. No cambiar formatos sin migración: ya hay datos en la placa.
- Escrituras a flash: usar `archivoGuardarSeguro()` / `archivoRecuperar()` de `archivo.h` (tmp+rename).
- El relé de frío tiene piso propio (`FRIO_MIN_OFF_S` = 180 s), no solo `cfg.minOffS`. Los límites
  absolutos (`SEG_*` en `hardware.h`) usan la lectura **sin** ajuste y no dependen de la pausa.
- La web, los comentarios y la UI son en español rioplatense. Mantener ese registro.

## Comportamiento actual no obvio

- `controlForzar()` devuelve `0` tanto si aplicó el forzado como si lo **rechazó en silencio**
  (test, OTA, límite absoluto) → `POST /api/manual` responde `ok:true` sin haber hecho nada.
- El campo `inicio` de `POST /api/balanza/seg` no se acota contra "ahora"; una fecha futura
  desordena `/pesaje.bin` y rompe la búsqueda binaria de la estimación de merma.
- Si el DHT22 falla, `est.sensorOk` sigue en `true` hasta `TIMEOUT_SENSOR_MS` (5 min, intencional):
  los relés mantienen su estado hasta entonces.

## Cómo se verifica

No hay tests ni CI. Ciclo real: `python paginas/generar.py` (si tocaste HTML) → `node subir.mjs` →
probar en el equipo. `GET /api/diag` devuelve versión, partición, motivo del último reinicio, heap y
estado de la balanza.

## Ruido esperado (no "arreglar")

- `build/` son artefactos de arduino-cli (~100 MB: `.elf`, `.map`, `.merged.bin`). No hay git ni
  `.gitignore` todavía.
- `build/ota/sketch/*.h` difiere de la raíz solo por el `#line` que agrega el preprocesador.
  Es normal, no es una desincronización.
- `camara_curado.ino:147` imprime HEAP cada 5 s; sigue marcado `TEMPORAL` desde la v41.
- Comentarios que dicen "Balanza Bluetooth" están obsoletos: la balanza es Wi-Fi desde la v41.
