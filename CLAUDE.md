# CLAUDE.md — Cámara de curado (secadero de salames)

## Qué es
Controlador de una heladera convertida en cámara de curado: ESP32 + DHT22 + 6 relés + encoder + LCD 20x4,
con web propia (Secadero / Ajustes / Balanza / Diario de secado). Desde la v41 lee por Wi-Fi la
**balanza** (`../balanza`) donde cuelga la **pieza testigo** y sigue su merma.
**Versión actual: v1.0.6 OTA** (29/10/2026, se actualiza por Wi-Fi). Historial de versiones en `plan.md`.

## Versión
- `config.h`: `SOFTWARE_VERSION` (LCD al arrancar, LCD > Diagnostico > Acerca de, pie de la web) y
  `VERSION_PROGRAMA` (Diagnóstico de la web; `paginas/generar.py` la copia a la página para detectar una
  página vieja en caché). Desde la v1.0.1 OTA las dos dicen lo mismo: cambiar las dos en cada versión.
- Numeración anterior: v40…v44 (internas). v44 = primera con OTA; v1.0.1 OTA = la misma + versión unificada;
  v1.0.2 OTA = arreglos de una revisión externa; v1.0.3 = web SIN clave; v1.0.4 = la balanza se consulta cada 3 s;
  v1.0.5/6 = parada de emergencia (APAGAR TODO) y botón de reiniciar (ver plan.md).

## Hardware
- ESP32 placa formato Uno ("ESP32 Dev Module"). Desde la v44, **partición propia `partitions.csv`**
  (`PartitionScheme=custom`): dos apps de 1,44 MB (OTA) + 1 MB de LittleFS; nvs en 0x9000 como siempre.
  Cambiar de esquema borra el historial, los eventos, el diario y los pesajes (la config en NVS queda).
- En esta PC suele estar en **COM3** (la balanza en COM4). Antes de subir por USB, identificar el puerto
  leyendo el serie SIN reiniciar (ver abajo; `T=... HR=...` es la cámara). El usuario a veces los nombra al
  revés. Por Wi-Fi no hace falta: `subir.mjs` va a 192.168.18.88.
- **Abrir COM3 de la forma común reinicia la placa y la deja en reset mientras el puerto está abierto**
  (DTR/RTS → EN; el ESP32 lo informa como "encendido normal"; relés apagados todo ese rato). Para leer
  sin reiniciar: `s = serial.Serial(); s.port='COM3'; s.baudrate=115200; s.dtr=False; s.rts=False; s.open()`.
  Nunca abrir el serie en el primer minuto después de un OTA: el programa nuevo todavía no está
  confirmado y el reinicio lo hace volver al anterior (pasó el 24/10/2026).
- Pines: `config.h` (DHT22) y `hardware.h` (relés, encoder, LCD, balanza).
- Desde el 24/10/2026 está en el **laboratorio, sin producto** (relés sin nada conectado): se puede probar.
  Cuando vuelva a tener producto es un equipo EN USO: al subir, los relés se apagan unos segundos. Si algo
  falla después de subir, el rollback del OTA vuelve solo al anterior; si no, volver YA a una versión que
  funcione (por ejemplo `USAR_BALANZA 0`) y recién después investigar.

## Memoria (importante)
- Con Wi-Fi + web + historial en RAM (~67 KB) + cliente de la balanza quedan **~94 KB de heap libre**
  (bloque mayor ~53 KB, mínimo visto ~80 KB) — medido el 17/10/2026 con la v41. Con la v1.0.1 OTA
  (24/10/2026): ~96 KB libres, bloque ~51 KB, mínimo ~88 KB.
- **Bluetooth no entra**: NimBLE hizo abortar al arrancar mDNS (lock_init_generic sin memoria). Por eso
  la balanza va por Wi-Fi. No volver a meter BLE sin liberar RAM antes.
- Hay un reinicio automático si el bloque libre mayor queda < 30 KB por 10 min (`camara_curado.ino`).

## Páginas web
- Se editan en `paginas/*.html`; `pagina.h`, `pagina_bitacora.h`, `pagina_wifi.h` se **generan** con
  `python paginas/generar.py` (gzip + copia `VERSION_PROGRAMA` de `config.h`). No editar los .h a mano.
- Todo POST lleva el encabezado `X-Camara: 1` (anti-CSRF). Desde la v1.0.3 la web **no pide clave** (`WEB_SIN_CLAVE 1` en `config.h`);
  con 0 vuelve el Digest (usuario `admin`, `WEB_CLAVE` de `config.h`). `subir.mjs` y el hub se adaptan solos.
- Para probar las páginas sin la placa: servidor simulado (node) que sirva `paginas/` y conteste las APIs.

## Balanza (v41)
- `balanza.cpp/.h`: tarea propia; busca `BALANZA_HOST.local` por mDNS, guarda la IP en NVS
  ("balanzaRed"), GET `/api/peso?cam=1` cada 1 s, POST `/api/orden`. Estados `CON_*`.
- `pesaje.cpp/.h`: seguimiento de la pieza testigo (NVS "pesaje": p0, objetivo, inicio, tanda);
  un punto cada 30 min en `/pesaje.bin` (8 bytes, máx. 8000); ritmo de merma de las últimas 72 h
  (mínimo 12 h) y días que faltan. Necesita hora NTP.
- La pieza de la balanza es **siempre la testigo**: en el Diario, la tanda vinculada toma la merma,
  el ritmo y los días de la balanza (aunque haya otras piezas marcadas como testigo).
- LCD: la pantalla principal **no se toca**. Todo va en Menú > Balanza (peso/merma, seguimiento
  Sigue/PAUSA, conexión ON/OFF, estado, buscar, olvidar IP). Tarar y calibrar solo desde la web.
- Pausa del seguimiento (v42): para descolgar la testigo (limpieza, inspección). Web (Balanza) y LCD;
  NVS "pau"/"pauD"; con la pieza descolgada (peso < 15 % del inicial) no se registran puntos.
- API: `GET /api/balanza`, `POST /api/balanza` (o=T|C<g>|R|A|AF|AX|M1|M0, t=<°C> opcional para T y C),
  `POST /api/balanza/red`, `POST /api/balanza/seg`, `GET /api/balanza/hist` (ver `web.h`).
- Temperatura (v43): la cámara manda `est.temp` en cada consulta (`/api/peso?cam=1&t=`) y la balanza
  compensa la deriva de la celda. `/api/balanza` trae el objeto `temp` (celda, aire, corr, comp, kc, ks,
  tara, calib, avn, av, apr). En la web: aviso arriba del peso, temperatura de tara/calibración con
  "Traer de la cámara" y el panel "Temperatura y compensación" (aprender la deriva). Coeficientes a mano y
  OTA de la balanza: en su propia página (balanza.local).

## Actualización por Wi-Fi (v44)
- `POST /api/ota` (multipart con el .bin) en `web.cpp`. La librería pasa el archivo ANTES de los
  middlewares: la clave (`acceso.allowed`) y `X-Camara` se controlan en el primer pedazo.
- Mientras graba, `controlOta(true)`: clima y ventilador apagados, automático suspendido (vuelve solo a
  los 5 min si se cuelga). Al terminar, reinicio desde `webLoop()` (vuelca historial y eventos antes).
- Rollback: `verifyRollbackLater()` = true; `webLoop()` confirma el programa nuevo al minuto si hay Wi-Fi
  (o está en modo configuración); a los 10 min sin Wi-Fi vuelve solo al anterior. Un reinicio antes de
  confirmar también vuelve al anterior.
- Para no pasarse de 1,44 MB: sin `HTTPClient` (traía ~95 KB de cifrado); la balanza se consulta con un
  HTTP mínimo sobre `WiFiClient` (`balanza.cpp`, `pedir()`). El diario tiene tope de 250 KB.
- **No consultar la balanza más seguido que cada 3 s**: a 1 s la cámara perdía el 5 % de los pings (una conexión TCP nueva por
  consulta). Medido el 29/10/2026: 5,0 % de pérdida a 1 s, 0,4 % a 3 s (ver `INTERVALO_MS` en `balanza.cpp`).
- Web: Ajustes > Diagnóstico > "Actualizar el programa por Wi-Fi". Diagnóstico muestra la partición.

## Compilar / subir
- Por Wi-Fi: `node subir.mjs` (compila y sube a 192.168.18.88).
- Por USB (primera vez o si el OTA no anda): `node subir.mjs COM3`.
- A mano: `arduino-cli compile -b esp32:esp32:esp32:PartitionScheme=custom --build-property upload.maximum_size=1507328 camara_curado`
  (sin el maximum_size, "custom" no avisa si el programa no entra).
Librerías: DHT (Adafruit), ESPAsyncWebServer + AsyncTCP (ESP32Async), ArduinoJson 7, hd44780.

## Documentación
- `docs/Menu_LCD.pdf`: manual de todo el menú del LCD. Se genera con `python docs/generar_menu_lcd.py`
  (las listas están copiadas de `menu.cpp`: si cambia el menú, actualizar el script y regenerar).
- `LEEME.txt`: notas de instalación para el usuario.

## Git / GitHub
- Repo propio en esta carpeta; `build/` no se sube. `config.h` tiene el Wi-Fi con marcadores de posición (se configura desde la web / modo
  configuración). **Nunca poner datos reales (Wi-Fi, claves, correo) en archivos que se suban.**

## Parada de emergencia (v1.0.5)
- `controlParada(bool)` (control.cpp): apaga los 6 relés (también frío y AUX) y suspende el control. **Se guarda en NVS ("parada")**:
  un reinicio, un OTA o un corte de luz NO la levantan; solo "REANUDAR TODO". Es independiente de la pausa (`cfg.activo`).
- El bloqueo está en `releEscribir()` (cualquier camino solo puede apagar) y además en `aplicar`/`aplicarDirecto`, `controlForzar`
  (409 motivo `parada`), AUX, test de relés y OTA. Sin protección de temperatura mientras dura.
- Web: botón rojo APAGAR TODO arriba (`POST /api/parada accion=on|off`; `/api/estado` trae `parada`). Evento 9 en el registro.
  LCD: ítem "APAGAR TODO" al final del menú (dos confirmaciones, cursor en NO); pantalla principal muestra `** PARADA TOTAL **`.
- Reiniciar: Ajustes > Diagnóstico > "Reiniciar la cámara" (`POST /api/reiniciar`; 409 si hay carga en curso o el programa nuevo
  todavía no se confirmó: reiniciar antes del minuto lo haría volver al anterior).
- Un botón por software no reemplaza cortar la corriente: si la ESP32 se cuelga o un contacto queda pegado, solo desenchufar sirve.

## Control manual
- `controlForzar()` devuelve 0 (hecho), `FORZAR_RECHAZADO` (test / ota / calibración / seguridad; motivo en
  `controlForzarMotivo()`) o los segundos de espera. La web contesta 409 con `motivo` o `espera`.

## Estilo
Comentarios y textos en español rioplatense; LCD sin acentos ni ñ. Seguir el estilo del código existente
(candados `ControlCandado`, NVS con Preferences, JSON con ArduinoJson y `enviarJson`).
