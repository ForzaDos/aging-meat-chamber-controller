#pragma once

// Registra las rutas y arranca el servidor HTTP (puerto 80).
//   GET  /               -> página principal (también /chart.js, la librería de gráficos)
//   GET  /api/estado     -> lecturas, relés y setpoints (JSON)
//   POST /api/config     -> guarda setpoints (form-urlencoded)
//   POST /api/manual     -> cual=luz|otros, estado=1|0  (AUX1/AUX2)  |  cual=hum|desh|frio, modo=auto|on|off
//   GET  /api/eventos    -> ?n=40&saltar=0 registro de relés
//   GET  /api/historial  -> ?rango=1h|1d|1w|1m|3m
//   GET  /api/test       -> estado del test de relés
//   POST /api/test       -> accion=iniciar&confirmar=SIN220[&modo=uno|suma|todos] | accion=detener
//   POST /api/calib      -> accion=iniciar|cancelar|aplicar  (calibración con sal común)
//   GET  /bitacora       -> página del diario de secado (el nombre interno quedó de "bitácora")
//   GET/POST /api/bitacora, GET /api/bitacora/info, GET /api/resumen  -> datos del diario
//   POST /api/borrar            -> confirmar=BORRAR: borra historial y registro de relés
//   POST /api/borrar/registro   -> confirmar=BORRAR: borra solo el registro de relés
//   POST /api/borrar/bitacora   -> confirmar=BORRAR: borra solo el diario
//   GET  /api/acerca     -> autor y versión (config.h)
//   GET  /wifi, GET/POST /api/wifi, GET /api/wifi/scan  -> elegir la red Wi-Fi
//   GET  /api/diag       -> memoria libre, hora, motivo del último reinicio
//   GET  /api/balanza    -> peso, conexión y seguimiento de la pieza testigo (404 si USAR_BALANZA es 0)
//   POST /api/balanza    -> o=T (tarar) | o=C500 (calibrar con 500 g) | o=R (borrar calibración)
//   POST /api/balanza/red   -> accion=on|off|reconectar|olvidar
//   POST /api/balanza/seg   -> accion=iniciar|editar|terminar [p0, obj, tanda, nombre, ini]
//   GET  /api/balanza/hist  -> registro del peso del seguimiento
//   POST /api/ota        -> archivo .bin (multipart): actualiza el programa por Wi-Fi y reinicia
//   POST /api/parada     -> accion=on|off: PARADA DE EMERGENCIA (apaga los 6 relés y suspende el control; sobrevive a reinicios)
//   GET  /api/coredump   -> volcado binario del último cuelgue (404 si no hay); /api/diag trae "volcado":true/false
//   POST /api/reiniciar  -> reinicia la ESP32 (409 si está cargando un programa o no confirmó el último)
void webInit();

// En cada vuelta del loop(): reinicio después de una actualización y confirmación del programa nuevo.
void webLoop();

// Anota que el próximo reinicio es por falta de memoria (se muestra en Diagnóstico).
void webMarcarReinicioPorMemoria();
