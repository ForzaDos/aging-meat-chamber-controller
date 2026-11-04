# Plan — Cámara de curado

## Estado
**v1.0.34 OTA (4/11/2026)**, en la heladera sin producto (relés con cosas reales conectadas). Clima, relés, LCD con menú, web con
historial y Diario de secado, balanza de la pieza testigo por Wi-Fi y actualización por Wi-Fi (OTA).

## Versiones
- v40: clima, relés, LCD, web, historial, Diario de secado.
- v41 (17/10): balanza de la pieza testigo por Wi-Fi.
- v42: web más liviana (menos pedidos al abrir, sin parpadeo de pestañas), pausa del seguimiento de merma
  (web y LCD), seguridad de actuadores, clave Digest cambiable desde la web, XSS del Diario.
- v43 (24/10): compensación de temperatura de la balanza.
- v44 (24/10): OTA. v1.0.1 OTA (24/10): la misma, con la versión unificada en LCD y web.
- v1.0.2 OTA (29/10): arreglos de una revisión externa (abajo).
- v1.0.3 OTA (29/10): la web ya no pide clave (cámara, pileta y balanza).
- v1.0.4 OTA (29/10): la balanza se consulta cada 3 s (antes 1 s); la cámara dejó de perder paquetes (5 % → 0,4 %).
- v1.0.5/1.0.6 OTA (29/10): parada de emergencia APAGAR TODO (web y LCD) y botón Reiniciar la cámara.
- v1.0.7 OTA (30/10): el botón APAGAR TODO de la web fue un pulsador redondo (no gustó).
- v1.0.8 OTA (30/10): APAGAR TODO es un botón rectangular con borde de franjas amarillas y negras, debajo de las lecturas y visible en todas las pestañas.
- v1.0.9 OTA (30/10): APAGAR TODO pasó a la barra de pestañas (siempre visible al bajar la página): fila propia en el celular, al lado de las pestañas en pantallas anchas.
- v1.0.10 OTA (30/10): arreglo del orden de las reglas CSS: en la tablet el botón es de verdad una quinta pestaña.
- v1.0.11 OTA (30/10): APAGAR TODO chico (media pestaña) y último de la barra, también en el Diario; la parada se resalta mucho en el registro de relés (renglón rojo, y franja roja PARADA en el gráfico de actividad); GET /api/coredump y motivos de watchdog separados para investigar el cuelgue del 30/10 01:12.
- v1.0.12 OTA (30/10): el test de relés ya no espera a que la heladera lleve 3 min apagada (se hace con los 220 V desconectados): sin cuenta atrás en el LCD ni en la web.
- v1.0.13 OTA (30/10): Pausar control es un botón discreto (menor jerarquía que APAGAR TODO) en la barra de pestañas, de todas las páginas.
- v1.0.14 OTA (30/10): arreglo del orden de reglas CSS en la tablet: APAGAR TODO es la 5.ª pestaña y Pausar (verde claro, discreto) queda abajo a la derecha en todos los tamaños.

## v41 — Balanza
- [x] Cliente de la balanza (`balanza.cpp`): primero por Bluetooth (NimBLE) → abortaba por falta de RAM
      → reemplazado por Wi-Fi (mDNS `balanza.local` + HTTP). `WiFi.setSleep(false)` se mantiene.
- [x] Seguimiento de la pieza testigo (`pesaje.cpp`): peso inicial, merma objetivo, tanda del Diario,
      registro cada 30 min, ritmo de 72 h, días que faltan y fecha estimada.
- [x] Menú LCD > Balanza (pantalla principal sin cambios).
- [x] Web: pestaña **Balanza** (peso en vivo, tarar, merma, estimación, gráfico con proyección,
      calibración, conexión) y panel chico en Secadero.
- [x] Diario: la pieza de la balanza cuenta como testigo de la tanda vinculada; su curva va al gráfico;
      la merma objetivo de la tanda se sincroniza con la balanza.
- [x] Subida a la placa (COM3): arranca estable, encuentra la balanza en balanza.local y la lee cada 1 s.

## v42 — Pausa del seguimiento y web
- [x] Botón Pausar/Retomar (web) y Menú > Balanza > Seguimiento Sigue/PAUSA (LCD).
- [x] Guarda de "pieza descolgada": no registra pesos < 15 % del inicial.
- [ ] Probar la pausa con la pieza real (solo se probó en el simulador).

## v43 — Compensación de temperatura de la balanza (24/10/2026)
- [x] Mandar la temperatura de la cámara a la balanza en cada consulta.
- [x] Web: aviso de temperatura arriba del peso, temperatura de tara/calibración ("Traer de la cámara"),
      panel "Temperatura y compensación" con aprendizaje de la deriva.

## v44 / v1.0.1 OTA — Actualización por Wi-Fi (24/10/2026)
- [x] Sacar `HTTPClient` (~95 KB de cifrado): HTTP mínimo sobre `WiFiClient` para la balanza.
- [x] `partitions.csv`: dos apps de 1,44 MB + 1 MB de LittleFS (programa: 1,31 MB, 86 %). Diario: tope 250 KB.
- [x] `POST /api/ota` con clave y X-Camara; clima apagado mientras graba; rollback si no confirma.
- [x] Web: Ajustes > Diagnóstico > "Actualizar el programa por Wi-Fi". `subir.mjs` para la PC.
- [x] Carga por USB una sola vez (datos borrados: eran de prueba) y OTA probado: confirma al minuto, sigue
      en la partición nueva tras un reinicio, y vuelve sola a la anterior si se reinicia antes de confirmar.
- [x] Versión unificada "v1.0.1 OTA" en LCD (arranque y Acerca de) y web (pie y Diagnóstico).
- [x] Manual del menú LCD: `docs/Menu_LCD.pdf`.
- [ ] Confirmar en la web que la balanza sigue conectada con el cliente HTTP nuevo.

## v1.0.2 OTA — Revisión externa (29/10/2026)
- [x] El aviso "reinicio por memoria baja" se consume una vez (antes quedaba pegado tras cualquier reinicio).
- [x] `controlForzar()` avisa cuando rechaza (test, OTA, calibración, seguridad) y la web contesta 409 con motivo.
- [x] No se puede forzar un relé durante la calibración (volver a automático sí).
- [x] Fecha de ingreso del seguimiento: no en el futuro ni de hace más de 2 años; el input de fecha tiene `max` = hoy.
- [x] Reserva del JSON del historial: 28 bytes por punto. Sacado el `HEAP libre` por serie. Comentarios de Bluetooth.
- [x] Git + GitHub (repo público).
- [ ] Mutex en `enviarBitacora` (lectura mientras se compacta) y límite de intentos de clave (`AsyncRateLimitMiddleware`).

## v1.0.3 / v1.0.4 OTA — Sin clave y web lenta (29/10/2026)
- [x] `WEB_SIN_CLAVE 1`: sin usuario ni clave en la web de la cámara (y la de la pileta y la de actualizar de la balanza).
- [x] La web tardaba hasta 20 s: era el saludo de la clave (Digest) más la pérdida de paquetes. Sin clave abre en 0,09 s.
- [x] Pérdida de paquetes de la cámara (solo ella; router, balanza y pileta 0 %): la causa era consultar la balanza cada 1 s.
      A 3 s: 0,4 %. Medido con ping, balanza prendida/apagada, 5 minutos cada fase.

## v1.0.5 / v1.0.6 OTA — Parada de emergencia y reinicio (29/10/2026)
- [x] APAGAR TODO: apaga los 6 relés y suspende el control; persiste en NVS (reinicio/OTA/corte de luz no la levantan).
- [x] Web: botón rojo arriba + aviso fijo; LCD: ítem al final del menú con dos confirmaciones; evento 9 en el registro.
- [x] Botón "Reiniciar la cámara" (Diagnóstico). Probado en la placa: parada activa → reinicio → sigue activa, relés apagados;
      forzado/test/AUX rechazados con 409 `parada`; la pausa del hub (`activo`) no la levanta; reanudar devuelve el control.
- [ ] Probar el LCD (menú APAGAR TODO / REANUDAR TODO y el aviso fijo) con la persona delante de la pantalla.
- [ ] El panel del hub no muestra la parada (se podría agregar el estado `parada` a su driver).

- v1.0.16 OTA (30/10): Diagnóstico: Reiniciar en naranja y con confirmación escrita (REINICIAR); el borrado dice exactamente qué datos borra; selector de archivo del OTA con el estilo de la página.
- v1.0.16 OTA (30/10): Diario de secado: la pestaña Balanza ya no desaparece unos segundos al entrar (la página arrancaba sin ella y la mostraba recién al contestar la balanza; ahora solo la oculta si el equipo contesta 404).
- v1.0.17 a v1.0.19 OTA (31/10): dos balanzas (la de 1 kg y la de 10 kg, `BALANZAS[]` en hardware.h): una tarea las recorre, cada una con su seguimiento, su archivo de historial y su espacio de NVS (la 0 conserva los de siempre); `/api/balanzas`, `?i=N` en la web, menú Balanza del LCD con selector y la web con una pestaña por balanza.
- v1.0.20 OTA (31/10): revisión de la v1.0.19. Menú: la pantalla de conexión pasaba un `String` a un `%s` (podía colgar el LCD). Web: los POST de balanza (`/api/balanza`, `/seg`, `/red`) exigen `i` (antes asumían la 0 y podían tarar la de 1 kg con la pieza testigo colgada). La cámara comprueba quién contesta: la 10 kg tiene que decir `host` en `/api/peso` y si contesta otra placa se descarta la IP; se sacó el segundo nombre `balanza-2` de la de 1 kg. Banderas entre tareas `volatile`. Hub: `peso` = balanza 0, `peso10` = balanza 1, la pieza testigo va en `pesoT`.
- v1.0.21 OTA (2/11): vigía de la web. El servidor web se colgó con el Wi-Fi vivo (ping y TCP andaban, ningún pedido contestaba) y no había cómo recuperarlo sin desenchufar. Una vez por minuto la cámara se pide a sí misma `/api/estado`; con 5 fallas seguidas se reinicia (motivo "reinicio automatico: la web no respondia"). Solo actúa si el pedido a sí misma funcionó alguna vez desde el arranque, y nunca con Wi-Fi caído, OTA, test ni calibración. Sin probar en un cuelgue real. Señal Wi-Fi débil donde está (-72 a -81 dBm): falta repetidor.
- v1.0.22 OTA (3/11): revisión de lo que hizo DeepSeek. El Diario de secado se llamaba a sí mismo sin fin cuando ninguna balanza tenía seguimiento activo (`dibujarGrafico` ↔ `cargarHistBalanza`): ahora el registro de peso se pide una vez por conjunto de seguimientos. Principal: `sel` nulo al pintar la balanza elegida. `subir.mjs` ya no trae la ruta del workspace de DeepSeek.
- Rediseño visual (3/11, subido como v1.0.22): las tres páginas usan la estética del panel del hub (domótica): fondo pizarra `#0f1216`, tarjetas redondeadas de 16 px, acento ámbar `#f2a83b`, pestañas en píldora, temperatura ámbar y humedad celeste, tipografía del sistema. Iconos de equipos del hub (frío, humidificador, deshumidificador, ventilador que gira, luz, enchufe) en las 6 salidas y en el tablero resumen. Gráficos Chart.js con curva monótona, relleno en degradé, rejilla punteada y tooltip oscuro. Solo CSS y presentación: nada de lógica, rutas ni datos. Las variables CSS conservan sus nombres (`--laton` ahora es el ámbar, `--pimenton` la temperatura) para no tocar el JS.
- v1.0.23 OTA (3/11): histéresis superior e inferior independientes, en humedad y en temperatura (`humHistInf/Sup`, `tempHistInf/Sup`). Humedad: el humidificador prende por debajo de objetivo − inf y el deshumidificador por encima de objetivo + sup; los dos apagan en el objetivo. Temperatura (solo hay frío): prende por encima de objetivo + sup y apaga en objetivo − inf; la inferior va de 0 a 5 °C y vale 0 por defecto, así que se comporta como antes. Al actualizar, los valores guardados se migran (`humHist` → las dos de humedad, `tempHist` → la superior, inferior de temperatura 0); las claves viejas se siguen escribiendo con el valor superior por si se vuelve a un programa anterior. La API sigue devolviendo `humHist` y `tempHist` (la superior) y acepta `humHist` (pone las dos de humedad) para el hub. Web: cuatro deslizables, regla con la franja entre inferior y superior, y el gráfico dibuja la franja y la línea del objetivo de cada curva. Menú del LCD: Hist. sup. e Hist. inf. en Temperatura y en Humedad. Gráfico: botón "Ocultar valores flotantes" (se acuerda en el navegador).
- v1.0.24 OTA (3/11): historial. El gráfico suma los rangos de 6 h y de 12 h: salen del nivel de 1 punto por minuto (`/api/historial?rango=6h|12h`, los últimos 360 o 720 registros; no usan memoria nueva). Ajustes > Borrar solo el historial de 1 mes o de 3 meses (`POST /api/borrar/historial`, `nivel=1m|3m`, pide escribir BORRAR): vacía ese nivel y borra su archivo de flash; sirve para empezar un secado nuevo. La RAM de cada nivel es fija, así que lo que se libera es flash. No toca el historial de hasta 1 semana, el registro de relés, el diario ni los ajustes.
- v1.0.34 OTA (4/11): la cámara elige sola el punto de acceso de mejor señal al conectarse (escaneo de todos los canales + orden por señal) y lo revisa cada 5 min (pasa a otro del mismo nombre si es ≥ 10 dB mejor; respaldo a los 25 s). Soltar el fijado ya no corta la conexión. Motivo: con el repetidor (mismo nombre de red) el ESP32 se conectaba al primero que encontraba, no al mejor.
- v1.0.33 OTA (4/11): diagnóstico del sensor DHT22 en `/api/diag` (objeto `sensor`: lecturas, fallas, racha, caídas, fallas tras cambio de relé, última lectura buena, nivel del pin) y en Ajustes > Diagnóstico, para ver cuándo y cada cuánto falla.
- v1.0.32 OTA (4/11): estando conectada a otro punto de acceso que el fijado, la cámara vuelve a probar el fijado cada 5 min (antes 30). Motivo: a las 16:34 hubo un corte de energía; la cámara arrancó antes que el repetidor, no encontró el punto de acceso fijado en 20 s y se conectó al router (−62/−84 dBm) en la IP .88. Ese mismo día, desde las 16:37, el sensor DHT22 no devuelve ninguna lectura (`sensorOk` falso, relés apagados): también tras reiniciar por software, o sea que parece del cable o del sensor, no del programa. Pendiente: revisar el DHT22 a mano.
- v1.0.31 OTA (4/11): (1) la cámara se puede fijar a un punto de acceso (BSSID) desde `/wifi`, con respaldo a cualquiera a los 20 s: con el repetidor (mismo nombre de red) se conectaba al primero que encontraba y alternaba entre .103 (repetidor, buena señal) y .88 (router, mala) en cada reinicio; (2) humidificador y deshumidificador se pueden dejar apagados (casillas en Setpoints y en cada perfil: `humOff`/`deshOff`); (3) `/api/diag` informa `mac` (la propia del ESP32; la usa el hub de domótica para reencontrar la cámara si cambia de IP).
- v1.0.30 OTA (4/11): los perfiles se editan sin tocar la cámara (botón Editar en el detalle de cada uno: mismos controles que Setpoints y Ventilador, "Guardar perfil" / "Guardar y cargar" / "Cancelar"). `POST /api/perfiles accion=editar`; `GET /api/perfiles` suma `tempBanda` y `ventAnclaMin`. Los cuatro nombres se pueden cambiar y los botones de los perfiles pasan debajo de los sliders (esto se subió primero como v1.0.29, sin el zoom). Gráficos: el zoom ×2/×4 ahora también actúa sobre la humedad (multiplica el rango que se ve; antes solo subía un alto mínimo de 10 % que los umbrales ya superaban); en temperatura sigue subiendo el alto mínimo. Red (4/11): con un repetidor Wi-Fi la cámara aparece con otra MAC (el repetidor le cambia el prefijo) y otra IP (.103); la reserva DHCP hay que hacerla con esa MAC.
- v1.0.28 OTA (4/11): perfiles de setpoints. Cuatro casilleros (de fábrica Estufado, Secado 1, Secado 2, Otros; los cuatro renombrables) en NVS (namespace `perfiles`, blob `p1..p4`) que guardan objetivos e histéresis de humedad y temperatura, banda de alarma, esperas de confirmación y programa del ventilador; no la calibración, los tiempos de protección, la pausa ni la parada. API `GET/POST /api/perfiles`; `/api/estado` trae `perfil` (cuál coincide). Web: cuatro botones debajo de los sliders de Ajustes > Setpoints que abren el detalle (Cargar / Guardar actual / cambiar nombre), etiqueta "Etapa" bajo la hora. Sin LCD ni hub.
- v1.0.27 OTA (3/11): web. Los gráficos a veces quedaban dibujados a un tercio de escala, arriba a la izquierda (iPhone, pixel ratio 3): el navegador reinicia el contexto del canvas y Chart.js solo fija la escala cuando cambia el tamaño. Plugin `reponerEscala` (setTransform antes de cada dibujo) en principal y bitácora, y redibujo al volver a la página (`visibilitychange`). Reproducido en la PC forzando el estado; la causa exacta en el celular no está confirmada. Solo páginas.
- v1.0.26 OTA (3/11): espera de confirmación para prender el humidificador y el deshumidificador (`humEsperaS`, `deshEsperaS`, 0 a 300 s, 60 por defecto): la humedad tiene que seguir pasada del límite todo ese tiempo; se pidió porque al arrancar el compresor la humedad cae a ~55 % por segundos y el humidificador prendía. Web (sliders y leyenda), menú LCD (Humedad) y NVS. Histéresis de humedad hasta 40 % (botones ±5). Antes de esto no había ningún filtro: el control reaccionaba a cada lectura de 3 s.
- v1.0.25 OTA (3/11): web. Ajustes: las histéresis (4) pasan de slider a botones − / + de 0,5 (el input queda oculto), y todos los sliders restantes se agarran solo de la perilla (`pointer-events:none` en la barra, `touch-action:pan-y`) para que al deslizar la página con el dedo no se muevan valores sin querer. Los tiempos de protección de los relés llevan leyendas (para qué sirven, qué hace cada uno, cuándo no esperan) y una línea en vivo con el ciclo mínimo y los ciclos por hora. Gráficos: selector de escala vertical Auto / ×2 / ×4 (sube el alto mínimo que muestra, 4 °C y 10 %; se recuerda en el navegador, `camZoom`). Se saca el degradado bajo las curvas (tapaba las líneas de histéresis). Solo páginas.

## Cuelgue del 30/10/2026: causa hallada (arreglo pendiente de aprobación)
- Datos: el historial deja de registrar entre las 01:04 y las 01:15; reinicio por watchdog a las 01:12:08 (v1.0.6), con la parada activa
  desde las 00:21. Volcado leído por Wi-Fi (`GET /api/coredump`, v1.0.11) y decodificado con la v1.0.6 reconstruida (otra carpeta, la firma
  SHA no coincide pero las funciones sí).
- **Causa:** `loopTask` estaba en `DHT::expectPulse` (DHT.cpp:387) ← `DHT::read` ← `readHumidity` ← `loop()` (camara_curado.ino:85), con las
  interrupciones bloqueadas. La librería Adafruit DHT cuenta vueltas de bucle y no tiempo (`_maxcycles` = 240000 a 240 MHz, cada vuelta es un
  `digitalRead`): cada espera fallida son decenas de ms y una lectura hace ~80 esperas. Un sensor que contesta el arranque y se corta a mitad de
  la trama bloquea varios segundos (> 300 ms del watchdog de interrupciones) y reinicia la placa. Un sensor sano no pasa por ahí.
- Disparador: el usuario estaba cambiando los cables del sensor con la cámara encendida (~20 min sin sensor); no hay un sensor defectuoso.
  Revisar el cableado del DHT22: VCC a 3V3, pull-up de 4,7–10 kΩ entre DATA y VCC, cable corto, GND real (no el pin falso junto al 5 V).
- **Decisión del 30/10/2026: no se arregla por ahora** (fase de pruebas en el laboratorio, nada conectado a los relés). Retomar al poner producto o cargas.
- Arreglo propuesto: lector propio del DHT22 con tiempos en µs (abortar la trama al primer timeout, interrupciones bloqueadas solo ~5 ms),
  contadores `dhtFallas`/`dhtMaxMs` en `/api/diag`, y poder borrar el volcado ya leído.
- Descartado: candados (los candados del control no intervienen: la lectura del sensor va antes del bloque con candado). El registro de eventos
  vacío es porque el usuario lo borra para pruebas.
- Si una carga por OTA falla ("other side closed") la cámara puede quedar con el control suspendido hasta 5 min (OTA_MAX_MS).

## Pendientes generales
- [ ] Seguir la memoria libre (Diagnóstico) durante 24 h.
- [ ] Primer seguimiento real: tarar, calibrar, colgar la pieza y "Empezar seguimiento".
- [ ] Si quedó un seguimiento activo de antes del borrado: terminarlo desde la web.

## Ideas a futuro
- Guardar el Diario de secado en el hub de la tablet (evaluado el 24/10: conviene cuando la tablet tenga
  copias de seguridad automáticas y arranque sola tras un corte de luz). Libera espacio en la cámara.
- Mostrar el aviso de temperatura de la balanza en el menú Balanza del LCD.
- Avisar (LCD/web) cuando la pieza llega a la merma objetivo.
- Usar el ritmo de merma para sugerir cambios de humedad (secado demasiado rápido = riesgo de costra).
- Liberar RAM (historial en flash) si algún día se quiere volver a Bluetooth.
