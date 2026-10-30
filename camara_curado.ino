/*
  Cámara de curado — ESP32 (placa formato Uno) + DHT22 + módulo de 6 relés + encoder + LCD I2C

  Librerías (Gestor de Librerías):
    - DHT sensor library (Adafruit) + Adafruit Unified Sensor
    - ESPAsyncWebServer y AsyncTCP (organización ESP32Async)
    - ArduinoJson (Benoit Blanchon), versión 7.x
    - hd44780 (Bill Perry), para la pantalla LCD I2C
  Placa: paquete "esp32 by Espressif Systems" 3.x -> "ESP32 Dev Module"

  Placa: Herramientas > Partition Scheme > "Custom": usa partitions.csv de esta carpeta
  (dos lugares de 1,44 MB para el programa, así se actualiza por Wi-Fi, y 1 MB para los datos).
  Antes de compilar: revisá config.h (sensor, tipo de módulo de relé; la red Wi-Fi se carga desde
  el celular) y hardware.h (pines de los 6 relés y qué controla cada uno, encoder y pantalla).
*/

#include <LittleFS.h>
#include <Wire.h>
#include <DHT.h>

#include "config.h"
#include "hardware.h"
#include "control.h"
#include "historial.h"
#include "eventos.h"
#include "bitacora.h"
#include "calibracion.h"
#include "redwifi.h"
#include "encoder.h"
#include "menu.h"
#include "pantalla.h"
#include "web.h"
#include "balanza.h"
#include "pesaje.h"

DHT dht(PIN_DHT, DHT_TIPO);   // tipo de sensor e intervalo de lectura: config.h

static uint32_t ultimaLectura = 0;

void setup() {
  controlInit();               // lo primero: relés apagados y setpoints cargados

  Serial.begin(115200);
  delay(300);
  Serial.println("\nCámara de curado - arranque");

  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(100000);       // 100 kHz: la velocidad del módulo I2C del LCD (más robusto ante el ruido de los relés)
  pantallaInit();
  encoderInit();
  menuInit();

  if (!LittleFS.begin(true)) Serial.println("LittleFS: error (se formateó o no hay partición)");

  dht.begin();
  historialInit();
  eventosInit();
  bitacoraInit();
#if USAR_BALANZA
  pesajeInit();                // seguimiento de la pieza testigo (balanza por Wi-Fi)
#endif

  // Con el encoder apretado al encender (1,5 s) se abre el modo de configuración de la red Wi-Fi.
  const bool configWifi = encoderMantenidoAlArrancar(1500);
  wifiInit(configWifi);        // conecta con la red guardada (config.h solo si no hay ninguna guardada)
  configTime(GMT_OFFSET_S, 0, NTP_SERVER_1, NTP_SERVER_2);

  webInit();
#if USAR_BALANZA
  balanzaInit();               // busca la balanza en la red, en su propia tarea
#endif
}

void loop() {
  const uint32_t ahora = millis();

  encoderLoop();               // pulsador del encoder

  // Lectura del sensor (fuera del candado: tarda unos milisegundos).
  bool hayLectura = false, ok = false;
  float t = NAN, h = NAN;
  if (ahora - ultimaLectura >= INTERVALO_LECTURA_MS) {
    ultimaLectura = ahora;
    hayLectura = true;
    h = dht.readHumidity();
    t = dht.readTemperature();
    ok = !isnan(h) && !isnan(t);
  }

  {
    ControlCandado candado;      // la web no cambia nada mientras el loop controla
    menuLoop();                  // gira / click: menú del LCD (puede cambiar objetivos y forzar relés)
    controlManualLoop();         // vence los forzados manuales (vuelven a automático)
    controlTestLoop();           // modo test de relés (si está activo)

    if (hayLectura) {
      if (ok) {
        est.tempCruda = t;
        est.humCruda  = h;
        t += cfg.offT;                                  // ajuste de calibración
        calibMuestra(t, h);                             // el asistente usa la humedad SIN ajustar
        h = constrain(h + cfg.offH, 0.0f, 100.0f);
      }

      controlActualizar(t, h, ok);
      if (ok && !controlTestActivo() && !calibActivo()) historialMuestra(t, h, (est.releHum ? 1 : 0) | (est.releDesh ? 2 : 0) | (est.releFrio ? 4 : 0) |
                                     (est.releVent ? 8 : 0) | (est.releOtros ? 16 : 0) | (est.releLuz ? 32 : 0) | (controlParadaActiva() ? 64 : 0));

      if (ok) Serial.printf("T=%.1f C (obj %.1f)  HR=%.1f %% (obj %.1f) | hum:%s des:%s frio:%s vent:%s aux1:%s aux2:%s\n",
                            t, cfg.tempSP, h, cfg.humSP,
                            est.releHum ? "ON" : "off", est.releDesh ? "ON" : "off", est.releFrio ? "ON" : "off",
                            est.releVent ? "ON" : "off", est.releLuz ? "ON" : "off", est.releOtros ? "ON" : "off");
      else    Serial.println("DHT22: lectura fallida (revisar cable de datos y alimentacion)");
    }
  }

  historialLoop();
#if USAR_BALANZA
  pesajeLoop();
#endif
  eventosLoop();
  pantallaLoop();

  wifiLoop();                  // reconexión, modo configuración y mDNS
  webLoop();                   // actualización por Wi-Fi: reinicio y confirmación del programa nuevo

  // Red de seguridad: si el bloque de memoria libre más grande se mantiene muy bajo durante 10 minutos
  // (fragmentación o fuga), se reinicia el equipo. Nunca durante el test de relés ni la calibración,
  // ni en la primera hora de funcionamiento. Al arrancar, el clima espera el tiempo mínimo apagado.
  static uint32_t ultMem = 0, memBajaDesde = 0;
  if (ahora - ultMem >= 60000UL) {
    ultMem = ahora;
    const bool baja = ESP.getMaxAllocHeap() < 30000UL;
    if (baja && !controlTestActivo() && !calibActivo()) {
      if (!memBajaDesde) memBajaDesde = ahora | 1;
      else if (ahora - memBajaDesde > 600000UL && ahora > 3600000UL) {
        Serial.println("Memoria baja sostenida: reinicio automatico");
        historialVolcar();       // no perder los últimos minutos
        eventosVolcar();
        webMarcarReinicioPorMemoria();
        delay(200);
        ESP.restart();
      }
    } else memBajaDesde = 0;
  }

  delay(10);
}
