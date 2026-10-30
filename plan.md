# Plan — Cámara de curado

## Estado
**v1.0.15 OTA en uso (30/10/2026)**, en el laboratorio sin producto. Clima, relés, LCD con menú, web con
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

- v1.0.15 OTA (30/10): Diagnóstico: Reiniciar en naranja y con confirmación escrita (REINICIAR); el borrado dice exactamente qué datos borra; selector de archivo del OTA con el estilo de la página.

## Cuelgue del 30/10/2026: causa hallada (arreglo pendiente de aprobación)
- Datos: el historial deja de registrar entre las 01:04 y las 01:15; reinicio por watchdog a las 01:12:08 (v1.0.6), con la parada activa
  desde las 00:21. Volcado leído por Wi-Fi (`GET /api/coredump`, v1.0.11) y decodificado con la v1.0.6 reconstruida (otra carpeta, la firma
  SHA no coincide pero las funciones sí).
- **Causa:** `loopTask` estaba en `DHT::expectPulse` (DHT.cpp:387) ← `DHT::read` ← `readHumidity` ← `loop()` (camara_curado.ino:85), con las
  interrupciones bloqueadas. La librería Adafruit DHT cuenta vueltas de bucle y no tiempo (`_maxcycles` = 240000 a 240 MHz, cada vuelta es un
  `digitalRead`): cada espera fallida son decenas de ms y una lectura hace ~80 esperas. Un sensor que contesta el arranque y se corta a mitad de
  la trama bloquea varios segundos (> 300 ms del watchdog de interrupciones) y reinicia la placa. Un sensor sano no pasa por ahí.
- Disparador de hardware: el sensor falló lecturas durante minutos (por eso no hay puntos de historial) hasta que una lectura tomó el camino largo.
  Revisar el cableado del DHT22: VCC a 3V3, pull-up de 4,7–10 kΩ entre DATA y VCC, cable corto, GND real (no el pin falso junto al 5 V).
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
