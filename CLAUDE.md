# CLAUDE.md — Cámara de curado (secadero de salames)

## Qué es
Controlador de una heladera convertida en cámara de curado: ESP32 + DHT22 + 6 relés + encoder + LCD 20x4,
con web propia (Secadero / Ajustes / Balanza / Diario de secado). Desde la v41 lee por Wi-Fi la
**balanza** (`../balanza`) donde cuelga la **pieza testigo** y sigue su merma.
**Versión actual: v1.0.35 OTA** (4/11/2026, se actualiza por Wi-Fi). Historial de versiones en `plan.md`.

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
  revés. Por Wi-Fi no hace falta: `subir.mjs` va a 192.168.18.103.
- **Abrir COM3 de la forma común reinicia la placa y la deja en reset mientras el puerto está abierto**
  (DTR/RTS → EN; el ESP32 lo informa como "encendido normal"; relés apagados todo ese rato). Para leer
  sin reiniciar: `s = serial.Serial(); s.port='COM3'; s.baudrate=115200; s.dtr=False; s.rts=False; s.open()`.
  Nunca abrir el serie en el primer minuto después de un OTA: el programa nuevo todavía no está
  confirmado y el reinicio lo hace volver al anterior (pasó el 24/10/2026).
- Pines: `config.h` (DHT22) y `hardware.h` (relés, encoder, LCD, balanza).
- Desde el 2/11/2026 está en la **heladera, sin producto, con los relés conectados a cosas reales**: avisar y pedir OK antes de comandos,
  reinicios o subidas (del 24/10 al 2/11 estuvo en el laboratorio sin nada conectado). Con producto es un equipo EN USO: al subir, los relés se apagan unos segundos. Si algo
  falla después de subir, el rollback del OTA vuelve solo al anterior; si no, volver YA a una versión que
  funcione (por ejemplo `USAR_BALANZA 0`) y recién después investigar.

## Red (4/11/2026)
- Hay un **repetidor Wi-Fi** cerca de la cámara (puesto el 4/11 porque la señal era mala). Las dos redes tienen el mismo nombre y **la cámara se
  conecta a una u otra según cuál encuentre al (re)conectarse**: por el repetidor tiene la IP **192.168.18.103** (−56 dBm estable, página en 0,2 s),
  directo al router tiene la **192.168.18.88** (−60/−75 dBm, 20 % de pérdida con paquetes grandes, páginas de 6-20 s). Las dos IP están reservadas
  en el router (cada una con su MAC). Cada reinicio (por ejemplo un OTA) puede cambiar de camino: antes de subir o monitorear, ver cuál está viva.
- Con el repetidor, el router ve la cámara con **otra MAC** (el repetidor le cambia el prefijo; la propia del ESP32, la que informa `/api/diag`
  desde la v1.0.30 como `mac`, no cambia). Por eso una reserva de IP hecha con la MAC original no valía y el router le dio otra IP.
  Si algún día se saca el repetidor, la cámara vuelve a verse con la MAC original: rehacer la reserva.
- **Estado (4/11/2026, 18:00): la cámara está en el laboratorio, SUELTA y eligiendo sola el mejor punto de acceso** (v1.0.34): quedó en el router, IP 192.168.18.88, −45 dBm. Probado: fijada al repetidor (−72 dBm) y soltada, en ~16 s se pasó sola al router. **Al llevarla a la heladera no hay que fijar nada**: allá el repetidor da −53 dBm y el router −84, así que debería quedar en el repetidor (IP .103) sola; si un corte de luz la hace arrancar antes que el repetidor, a los 5 min lo encuentra y se pasa. Sin probar en la heladera.
- **Elige sola el punto de acceso de mejor señal (v1.0.34)**: `conectar()` (`redwifi.cpp`) usa `WIFI_ALL_CHANNEL_SCAN` + `WIFI_CONNECT_AP_BY_SIGNAL` y, ya conectada y sin fijar, cada 5 min escanea
  y pasa a otro punto de acceso del mismo nombre si es ≥ 10 dB mejor (si el elegido no deja conectar en 25 s usa el primero que responda y espacia el chequeo a 30 min). Soltar el fijado ya no corta
  la conexión: en ~10 s revisa si hay uno mejor. Con esto no hace falta fijar nada para ir y volver entre el laboratorio (router) y la heladera (repetidor).
- **Punto de acceso fijado (v1.0.31)**: en `/wifi` (sección "Punto de acceso") se puede fijar la cámara al BSSID del repetidor (o soltarla). Se guarda
  en NVS (`redwifi`/`bssid`); al conectar intenta primero ese y, si en 20 s no lo logra, usa cualquiera (`conectar()` y el respaldo en `wifiLoop`);
  estando en otro, cada 5 min vuelve a probar el fijado. Cambiar de red lo borra. API: `GET /api/wifi` trae `bssid` y `fijo`, `/api/wifi/scan` trae
  `aps` (los puntos de acceso de la red guardada), `POST /api/wifi/fijar` (`bssid=` | `accion=quitar`).
- Todo apunta a .103: `subir.mjs`, `AGENTS.md`, `LEEME.txt`, el hub de domótica (`config.json`). El hub, si no la encuentra en su IP, la
  busca en la red por la `mac` de `/api/diag` (`domotica/hub/src/drivers/http/buscar.js`).
- Al monitorear una subida por OTA, mirar la IP actual: un monitor apuntado a la IP vieja no la ve volver (pasó el 4/11).

## Diagnóstico del sensor (v1.0.33)
- `controlActualizar()` cuenta lecturas, fallas, fallas seguidas, la peor racha, caídas (`sensorOk` pasa a falso después de `TIMEOUT_SENSOR_MS` = 5 min sin
  lectura buena: hasta entonces la web muestra el último valor congelado) y fallas dentro de los 6 s siguientes a que un relé cambió (para distinguir
  interferencia de un cable flojo). Va en `/api/diag` > `sensor` (más `pin`: nivel del cable de datos en reposo, tiene que ser alto) y en Ajustes >
  Diagnóstico. Los contadores son de RAM: se reinician con cada arranque.
- Caso del 4/11/2026: tras un corte de luz el DHT22 dejó de leer a los pocos minutos, dos veces seguidas, y volvía al reconectar el cable.

## Memoria (importante)
- Con Wi-Fi + web + historial en RAM (~67 KB) + cliente de la balanza quedan **~94 KB de heap libre**
  (bloque mayor ~53 KB, mínimo visto ~80 KB) — medido el 17/10/2026 con la v41. Con la v1.0.1 OTA
  (24/10/2026): ~96 KB libres, bloque ~51 KB, mínimo ~88 KB.
- **Bluetooth no entra**: NimBLE hizo abortar al arrancar mDNS (lock_init_generic sin memoria). Por eso
  la balanza va por Wi-Fi. No volver a meter BLE sin liberar RAM antes.
- Vigía de la web (v1.0.21): se reinicia sola si `/api/estado` (pedido a sí misma, 1 por minuto) falla 5 veces seguidas.
- Hay un reinicio automático si el bloque libre mayor queda < 30 KB por 10 min (`camara_curado.ino`).

## Páginas web
- Se editan en `paginas/*.html`; `pagina.h`, `pagina_bitacora.h`, `pagina_wifi.h` se **generan** con
  `python paginas/generar.py` (gzip + copia `VERSION_PROGRAMA` de `config.h`). No editar los .h a mano.
- Estética: la del hub de domótica (colores, tarjetas redondeadas, iconos de equipos, gráficos con degradé). Los colores están como variables en `:root` de cada página (`--laton` = ámbar de acento) y en `COL_T`/`COL_H`/`RELES` del JS; si se cambia uno, cambiarlo en las tres páginas. Los iconos de las salidas son SVG en línea (`ICO_EQ`, `icEq()` en `principal.html`).
- Espera de confirmación (v1.0.26): el humidificador y el deshumidificador solo prenden si la humedad sigue pasada del límite `humEsperaS` / `deshEsperaS` segundos (0..300, por defecto 60; claves NVS `humEsp`/`deshEsp`). Está en `controlActualizar` (`pideHumDesde`/`pideDeshDesde`): se reinicia en pausa, parada, test, OTA, sensor caído y forzado manual; una vez prendido sigue hasta el objetivo. El rango de la histéresis de humedad llega a 40 %.
- Humidificador y deshumidificador se pueden **dejar apagados** (v1.0.31): `cfg.humOff` / `cfg.deshOff` (NVS `humOff`/`deshOff`, por defecto false = en uso). Con el flag, el automático no los prende (quedan apagados, sin respaldo del otro) y, si estaban prendidos, se apagan respetando el mínimo encendido; el forzado manual y el límite de seguridad siguen. Van en los perfiles (campos `v2`, `humOff`, `deshOff` al final de `Perfil`: un perfil de la v1.0.28-30 es más corto y se lee con los dos en uso) y en la web (casillas "Usar el humidificador/deshumidificador" en Setpoints y en el editor de perfiles; la API manda `humOff`/`deshOff`). **Se avisa "NO SE USA" (v1.0.35)**: en las filas Salidas de la web de la cámara, en el hub (entidades `hum_off`/`desh_off`, `secadero.js`) y en el LCD (fila de solo lectura "Humidif: NO SE USA" arriba del submenú Humedad; no se cambia desde el LCD). **El LCD también muestra la etapa**: primera fila del menú principal, "Etapa: Estufado", o "Etapa: Estufado*" si se cambió algo después de cargarlo (`controlPerfilEtapa()`, NVS `perfCarg` = último perfil cargado o guardado); solo lectura, y la pantalla principal no se toca.
- Perfiles de setpoints (v1.0.28): 4 casilleros (de fábrica Estufado, Secado 1, Secado 2, Otros; los cuatro se pueden renombrar) en NVS, namespace `perfiles`, un blob `p1..p4` (`struct Perfil` en `control.h`; los campos solo se agregan AL FINAL). Guardan objetivos e histéresis de humedad y temperatura, banda de alarma, esperas de confirmación y programa del ventilador; NO la calibración, los tiempos de protección, la pausa ni la parada. `controlPerfilCargar` aplica con los mismos rangos que al arrancar y reinicia la espera de humedad. `/api/estado` trae `perfil` (1..4 si los ajustes coinciden con uno, 0 si no; usa una copia en RAM). Web: cuatro botones debajo de los sliders de Ajustes > Setpoints (cada uno abre su detalle con Cargar / Guardar actual / cambiar nombre) y "Etapa: …" bajo la hora. Cada perfil se puede **editar sin tocar la cámara**: `POST /api/perfiles accion=editar` (mismos campos que `/api/config`; parte de lo que ya tiene el perfil o, si está vacío, de los ajustes actuales) → `controlPerfilEditar()` aplica los límites y solo escribe el blob. En la web el editor (`CAMPOS_ED` en `principal.html`, campos `e_*`) reutiliza `salida()` y los botones − / + (`data-p`); "Guardar y cargar" manda `editar` y después `cargar`. Sin LCD ni hub.
- Histéresis: superior e inferior independientes (humedad y temperatura). Los rangos están repetidos en `hardware.h` (`HIST_*`), `control.cpp`, `web.cpp`, `menu.cpp` y los deslizables de `principal.html`: si se cambian, cambiarlos en todos. La API conserva `humHist`/`tempHist` (= la superior) por compatibilidad; **`humHist` pone las dos de humedad, así que mandarlo pisa la inferior**. El hub de domótica ya usa las claves nuevas (`humHistInf/Sup`, `tempHistInf/Sup`, `humEsperaS`, `deshEsperaS`, `humOff`, `deshOff`) y nunca manda las viejas (4/11/2026).
- Historial: 5 niveles (`historial.h`). Los rangos 6 h y 12 h de la web no son niveles: se piden como `rango=6h|12h` y salen de los últimos 360/720 registros del nivel de 1 minuto. Borrar solo 1 mes o 3 meses: `historialBorrarNivel()` (niveles 2 y 3).
- Todo POST lleva el encabezado `X-Camara: 1` (anti-CSRF). Desde la v1.0.3 la web **no pide clave** (`WEB_SIN_CLAVE 1` en `config.h`);
  con 0 vuelve el Digest (usuario `admin`, `WEB_CLAVE` de `config.h`). `subir.mjs` y el hub se adaptan solos.
- Para probar las páginas sin la placa: servidor simulado (node) que sirva `paginas/` y conteste las APIs.

## Balanzas (v41; dos desde la v1.0.17)
- **Dos balanzas** (hardware.h, tabla `BALANZAS[]`): índice 0 = la de 1 kg del secadero
  (`balanza.local`, NVS "balanzaRed"/"pesaje", archivo `/pesaje.bin` — lo ya registrado sigue igual);
  índice 1 = la de 10 kg (`balanza10k.local`, NVS "balanzaRed1"/"pesaje1", archivo `/pesaje1.bin`).
  `BALANZAS[i].alt` es un segundo nombre por si mDNS le agregó sufijo ("balanza10k-2").
- `balanza.cpp/.h`: **una sola tarea** que recorre las balanzas (una tarea por balanza no entra en RAM);
  busca por mDNS, guarda la IP en NVS, GET `/api/peso?cam=1`, POST `/api/orden`. Estados `CON_*`.
  La web y el LCD leen el estado cacheado: una balanza caída nunca bloquea una respuesta.
- `pesaje.cpp/.h`: **un seguimiento por balanza** (cada una con su pieza testigo, su objetivo, su tanda);
  un punto cada 30 min en su archivo; ritmo de merma de las últimas 72 h (mínimo 12 h) y días que
  faltan. Necesita hora NTP. El tope de `p0` es la capacidad de esa celda (1100 g / 11000 g).
- La pieza de la balanza es **siempre la testigo**: en el Diario, la tanda vinculada toma la merma,
  el ritmo y los días de la balanza (aunque haya otras piezas marcadas como testigo).
- LCD: la pantalla principal **no se toca**. Menú > Balanza muestra las dos (peso, merma y días que
  faltan) y el primer ítem, "Balanza", elige cuál se opera: las órdenes (seguimiento Sigue/PAUSA,
  conexión ON/OFF, estado, buscar, olvidar IP) son de la elegida. Tarar y calibrar solo desde la web.
- Web: **el peso de las dos balanzas se ve siempre** (dos columnas en "Peso en vivo", cada una con sus
  chips de estado). Abajo, un selector 1 kg / 10 kg decide cuál operan los paneles de trabajo (pieza
  testigo, calibración, temperatura, conexión). Los gráficos (Evolución y el Diario) dibujan una curva
  por balanza y tienen **casillas para prender y apagar cada una**. El panel chico del Secadero muestra
  el peso de la otra balanza conectada. El Diario vincula la tanda con la balanza que tenga esa pieza
  testigo (cualquiera de las dos).
- Pausa del seguimiento (v42): para descolgar la testigo (limpieza, inspección). Web (Balanza) y LCD;
  NVS "pau"/"pauD"; con la pieza descolgada (peso < 15 % del inicial) no se registran puntos.
- API: `GET /api/balanzas` (todas, cada una con su `seg`; es la que usa la web),
  `GET /api/balanza?i=N` (una sola, forma plana de siempre, compatibilidad),
  `POST /api/balanza` (o=T|C<g>|R|A|AF|AX|M1|M0, t=<°C> opcional para T y C),
  `POST /api/balanza/red`, `POST /api/balanza/seg`, `GET /api/balanza/hist?i=N`.
  Los POST aceptan `i=N` (0 por defecto) para elegir la balanza; el tope de calibración es la
  capacidad de esa celda.
- **Si una balanza no está conectada** el JSON la devuelve igual con `con:false` y sin `g`: la web la
  muestra como "sin conexión" y el resto de la página sigue funcionando (probado apagando una desde
  `POST /api/balanza/red accion=off`).
- Temperatura (v43): la cámara manda `est.temp` en cada consulta (`/api/peso?cam=1&t=`) y la balanza
  compensa la deriva de la celda. `/api/balanza` trae el objeto `temp` (celda, aire, corr, comp, kc, ks,
  tara, calib, avn, av, apr). En la web: aviso arriba del peso, temperatura de tara/calibración con
  "Traer de la cámara" y el panel "Temperatura y compensación" (aprender la deriva). Coeficientes a mano y
  OTA de la balanza: en su propia página (balanza.local).

- **Rendimiento (v1.0.19)**: la tarea de balanzas corre con prioridad 0 (la del idle) y, si una
  balanza no contesta, se la consulta cada 10 s y se busca por mDNS cada 60 s (tope 1,2 s). Antes,
  con una balanza apagada, la búsqueda por mDNS bloqueaba 2 s y la web tenía picos de 2-10 s por
  pedido: el hub de domótica la daba por "no responde" (su timeout es 5 s). Ahora /api/estado
  responde en 20-80 ms y la página en 65-80 ms.

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
- Por Wi-Fi: `node subir.mjs` (compila y sube a 192.168.18.103).
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
