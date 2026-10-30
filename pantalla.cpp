#include "pantalla.h"
#include "hardware.h"
#include "config.h"
#include "control.h"
#include "menu.h"
#include "redwifi.h"
#include "calibracion.h"
#include <Wire.h>
#include <WiFi.h>

#if PANTALLA_TIPO == 0
// ======================= sin pantalla =======================
void pantallaInit() {}
void pantallaLoop() {}

#else
// ======================= LCD con módulo I2C (hd44780) =======================
#include <hd44780.h>
#include <hd44780ioClass/hd44780_I2Cexp.h>

#if PANTALLA_TIPO == 1
  #define LCD_COLS 20
  #define LCD_ROWS 4
#else
  #define LCD_COLS 16
  #define LCD_ROWS 2
#endif

static hd44780_I2Cexp lcd;      // encuentra solo la dirección del módulo I2C
static bool hayPantalla = false;
static uint32_t ultDibujo = 0;
static uint8_t  pagina = 0;     // la última fila del 20x4 rota entre varios mensajes

static char cache[LCD_ROWS][LCD_COLS + 1];   // lo último escrito en cada fila: solo se redibuja lo que cambió

static void fila(uint8_t r, const char* s) {
  char b[24];
  snprintf(b, sizeof(b), "%-*.*s", LCD_COLS, LCD_COLS, s);   // rellena con espacios para borrar lo anterior
  if (strcmp(b, cache[r]) == 0) return;
  strcpy(cache[r], b);
  lcd.setCursor(0, r);
  lcd.print(b);
}

static void formatoMMSS(char* b, size_t n, int32_t s) {
  snprintf(b, n, "%d:%02d", (int)(s / 60), (int)(s % 60));
}

static const char* onoff(bool v) { return v ? "ON" : "--"; }

// 16x2: letra de AUX1/AUX2 = primera letra de su nombre (o 1 / 2 si coinciden).
[[maybe_unused]] static char letraAux(uint8_t i) {
  const char a = toupper(cfg.nomAux1[0]), b = toupper(cfg.nomAux2[0]);
  if (a == b) return i ? '2' : '1';
  return i ? b : a;
}

static void dibujar() {
  char b[32];

  if (menuActivo() || controlTestActivo()) {   // el menú (o el test de relés) ocupa toda la pantalla
    char l[4][21];
    if (menuActivo()) menuLineas(l, LCD_ROWS);
    else              menuLineasTest(l, LCD_ROWS);
    for (uint8_t r = 0; r < LCD_ROWS; r++) fila(r, l[r]);
    return;
  }

#if PANTALLA_TIPO == 1
  // ---------------- 20x4 ----------------
  //  T  14.3C  obj 14.0C
  //  H  76.4% obj 75.0%
  //  Fri:-- Hum:ON Des:--
  //  V:ON AUX1:-- AUX2:--   (alterna con avisos y cuentas regresivas)
  if (!est.sensorOk) {
    fila(0, "SIN SENSOR");
    snprintf(b, sizeof(b), "obj %4.1fC  %4.1f%%", cfg.tempSP, cfg.humSP);
    fila(1, b);
  } else {
    snprintf(b, sizeof(b), "T %5.1fC  obj %4.1fC", est.temp, cfg.tempSP);
    fila(0, b);
    snprintf(b, sizeof(b), "H %5.1f%% obj %4.1f%%", est.hum, cfg.humSP);
    fila(1, b);
  }
  snprintf(b, sizeof(b), "Fri:%s Hum:%s Des:%s", onoff(est.releFrio), onoff(est.releHum), onoff(est.releDesh));   // en el orden de los relés 1, 2, 3
  fila(2, b);

  // La última fila rota cada 4 s entre: ventilador/AUX1/AUX2, cuenta regresiva de forzados manuales y avisos (pausa, alarma, sin Wi-Fi).
  const char* nom[3] = { "Hum", "Des", "Fri" };
  char pg[10][24];
  uint8_t np = 0;
  // Ventilador, AUX1 y AUX2 (de los nombres se ven las 4 primeras letras; si no entra, "Ven" pasa a "V").
  snprintf(pg[np], 24, "Ven:%s %.4s:%s %.4s:%s", onoff(est.releVent), cfg.nomAux1, onoff(est.releLuz), cfg.nomAux2, onoff(est.releOtros));
  if (strlen(pg[np]) > LCD_COLS)
    snprintf(pg[np], 24, "V:%s %.4s:%s %.4s:%s", onoff(est.releVent), cfg.nomAux1, onoff(est.releLuz), cfg.nomAux2, onoff(est.releOtros));
  np++;
  for (uint8_t i = 0; i < 3; i++) {
    if (est.manual[i] == MAN_AUTO) continue;
    char t[8];
    formatoMMSS(t, sizeof(t), est.manualSeg[i]);
    snprintf(pg[np++], 24, "MANUAL %s %s %s", nom[i], est.manual[i] == MAN_ON ? "ON" : "OFF", t);
  }
  if (wifiEnConfig()) {               // modo configuración de Wi-Fi: tres mensajes que rotan
    snprintf(pg[np++], 24, "CONFIG WI-FI (celu)");
    snprintf(pg[np++], 24, "%s", WIFI_AP_NOMBRE);
    snprintf(pg[np++], 24, "-> %s/wifi", WIFI_AP_IP);
  }
  if (calibActivo()) {
    snprintf(pg[np++], 24, "CALIBRANDO SENSOR");
    snprintf(pg[np++], 24, "Clima apagado");
  } else if (!cfg.activo) {
    snprintf(pg[np++], 24, "CONTROL EN PAUSA");
  } else if (est.alarmaTemp) {
    snprintf(pg[np++], 24, "ALARMA TEMPERATURA");
  } else if (WiFi.status() != WL_CONNECTED) {
    snprintf(pg[np++], 24, "Sin Wi-Fi");
  }   // la IP se ve en el menú: Diagnostico
  if (controlParadaActiva()) fila(3, "** PARADA TOTAL **");   // fijo, sin rotar: tiene prioridad sobre todo
  else                       fila(3, pg[pagina % np]);
#else
  // ---------------- 16x2 ----------------
  //  T 14.3C H 76.4%
  //  14.0/75.0 FHDVLO   (objetivos; letra = relé encendido, guion = apagado)
  if (!est.sensorOk) snprintf(b, sizeof(b), "SIN SENSOR");
  else               snprintf(b, sizeof(b), "T%5.1fC H%5.1f%%", est.temp, est.hum);
  fila(0, b);
  // Letra mayúscula = encendido automático, minúscula = encendido a mano, x = apagado a mano.
  auto letra = [](bool on, uint8_t man, char c) -> char {
    if (man == MAN_ON)  return (char)(c + 32);
    if (man == MAN_OFF) return 'x';
    return on ? c : '-';
  };
  snprintf(b, sizeof(b), "%4.1f/%4.1f %c%c%c%c%c%c", cfg.tempSP, cfg.humSP,
           letra(est.releFrio, est.manual[R_FRIO], 'F'),
           letra(est.releHum, est.manual[R_HUM], 'H'), letra(est.releDesh, est.manual[R_DESH], 'D'),
           est.releVent ? 'V' : '-', est.releLuz ? letraAux(0) : '-', est.releOtros ? letraAux(1) : '-');
  if (controlParadaActiva()) snprintf(b, sizeof(b), "PARADA TOTAL");   // fijo: tiene prioridad
  fila(1, b);
#endif
}

void pantallaInit() {
  const int estado = lcd.begin(LCD_COLS, LCD_ROWS);
  hayPantalla = (estado == 0);
  if (!hayPantalla) {
    Serial.println("Pantalla LCD no encontrada: revisar SDA/SCL y alimentacion");
    return;
  }
  memset(cache, 0, sizeof(cache));
  fila(0, "Camara de curado");
  fila(1, "Iniciando...");
#if LCD_ROWS >= 4
  fila(2, "Version " SOFTWARE_VERSION);          // se ven mientras arranca (hasta que conecta el Wi-Fi)
  fila(3, SOFTWARE_AUTOR);
#endif
}

void pantallaLoop() {
  if (!hayPantalla) return;
  const uint32_t ahora = millis();
  if (ahora - ultDibujo < ((menuActivo() || controlTestActivo()) ? 60UL : 500UL)) return;   // en el menú responde enseguida al giro
  ultDibujo = ahora;
  pagina = (uint8_t)((ahora / 4000) % 60);   // la última fila cambia de mensaje cada 4 s

  // Cada 15 minutos se reinicia la pantalla y se redibuja todo: si el ruido de un relé o del
  // compresor le metió basura, se corrige sola (solo se redibuja lo que cambia).
  static uint32_t ultReinicio = 0;
  if (ahora - ultReinicio >= 900000UL && !menuActivo() && !controlTestActivo()) {
    ultReinicio = ahora;
    lcd.begin(LCD_COLS, LCD_ROWS);
    memset(cache, 0, sizeof(cache));
  }
  dibujar();
}
#endif
