#include "control.h"
#include "config.h"
#include "hardware.h"
#include "eventos.h"
#include "calibracion.h"
#include <Preferences.h>
#include <time.h>
#ifdef ARDUINO_ARCH_ESP32
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <esp_timer.h>
static SemaphoreHandle_t candado = nullptr;
void controlBloquear() { if (candado) xSemaphoreTakeRecursive(candado, portMAX_DELAY); }
void controlLiberar()  { if (candado) xSemaphoreGiveRecursive(candado); }
static int64_t segDesdeArranque() { return esp_timer_get_time() / 1000000; }   // no se reinicia a los 49 días como millis()
#else
void controlBloquear() {}
void controlLiberar()  {}
static int64_t segDesdeArranque() { return millis() / 1000; }
#endif

Config cfg;
Estado est;

static Preferences prefs;
static uint32_t cambioHum = 0, cambioDesh = 0, cambioFrio = 0;   // millis() del último cambio
static uint32_t ultimaLecturaOk = 0;
static uint8_t  perfilCargado = 0;                               // 1..4: último perfil cargado o guardado (NVS "perfCarg"); lo usa el LCD para decir "modificada"
static uint32_t pideHumDesde = 0, pideDeshDesde = 0;             // millis() desde que la humedad pasó el límite de encendido (0 = no lo pasó)
// Humidificador (v1.0.39): promedio lento, tanda de pulsos y bloqueo después del frío. Solo del humidificador: el deshumidificador no se toca.
#define PROM_TAU_S     600.0f     // constante de tiempo del promedio lento (10 min)
#define BLOQ_MAX_MS    1800000UL  // un bloqueo nunca dura más de 30 min seguidos
static float    promHum = 0;                                      // promedio exponencial de la humedad
static bool     promOk = false;                                   // false = hay que arrancarlo con la próxima lectura
static uint32_t promUlt = 0, promIni = 0;                         // millis() de la última muestra y del arranque
static uint32_t bajoAcumMs = 0;                                   // tiempo con el promedio bajo el límite (el frío y su bloqueo no cuentan)
static uint32_t bloqDesde = 0;                                    // millis() desde que empezó el bloqueo actual (0 = no bloqueado)
static bool     tanda = false;                                    // tanda de pulsos en curso
static bool     valv = false;                                     // pulso de la válvula en curso (humedad muy baja durante el bloqueo): no toca la tanda ni el contador
static bool     promPrev = false;                                 // el promedio estaba activo en la vuelta anterior (si cambia, se arranca de cero)
static void humReiniciar() { valv = false; promOk = false; bajoAcumMs = 0; bloqDesde = 0; tanda = false; est.humProm = NAN; est.humBajoMin = 0; est.humFase = 0; }
static uint32_t manualFin[3] = {0, 0, 0};                         // millis() en que vence cada forzado

// Límites absolutos (hardware.h, SEG_*), con la lectura sin ajuste. Los calcula controlActualizar().
static bool segCalor = false;     // frío obligado a encender
static bool segFrioNo = false;    // frío obligado a apagar
static bool segHumNo = false;     // humidificador obligado a apagar
static bool segDeshNo = false;    // deshumidificador obligado a apagar

// Datos de cada relé de clima, indexados por R_HUM / R_DESH / R_FRIO.
static bool*     const ESTADO[3] = { &est.releHum, &est.releDesh, &est.releFrio };
static const uint8_t   PIN[3]    = { PIN_RELE_HUM, PIN_RELE_DESH, PIN_RELE_FRIO };
static const uint8_t   EVC[3]    = { EV_HUM, EV_DESH, EV_FRIO };
static uint32_t* const CAMBIO[3] = { &cambioHum, &cambioDesh, &cambioFrio };

// Las 6 salidas en orden de función (hum, desh, frío, vent, luz, otros). El test las recorre en el orden físico IN1..IN6.
static const uint8_t     PIN6[TEST_RELES]    = { PIN_RELE_HUM, PIN_RELE_DESH, PIN_RELE_FRIO, PIN_RELE_VENT, PIN_RELE_LUZ, PIN_RELE_OTROS };
static bool* const       EST6[TEST_RELES]    = { &est.releHum, &est.releDesh, &est.releFrio, &est.releVent, &est.releLuz, &est.releOtros };
static const char* const NOMBRE6[TEST_RELES] = { "Humidificador", "Deshumidificador", "Refrigeracion", "Ventilador", nullptr, nullptr };   // 4 y 5: nombres de AUX1/AUX2

// Estado del test de relés
static volatile int8_t testPedido = 0;     // 1..3 = iniciar ese modo, -1 = detener (lo pide la web desde otra tarea)
static bool     testOn = false;
static uint32_t testIni = 0;
static uint8_t  testModo = TEST_UNO;
static uint8_t  testMask = 0;            // bit i = función i encendida por el test
static bool     luzPrev = false, otrosPrev = false;

// PARADA DE EMERGENCIA (ver controlParada, al final): todo apagado y el control suspendido hasta reanudar.
// Se guarda en NVS: un reinicio no la levanta. releEscribir() la respeta en cualquier camino.
static volatile bool paradaOn = false;

// Actualización por Wi-Fi en curso (ver controlOta, al final)
static volatile bool otaOn = false;
static uint32_t otaIni = 0;
#define OTA_MAX_MS  300000UL             // si la carga no termina en 5 min, el control vuelve solo

const uint8_t PERIODOS_VENT[8] = { 1, 2, 3, 4, 6, 8, 12, 24 };   // dividen 24 h: los ciclos caen siempre a la misma hora

// ---------------------------------------------------------------------
//  Salida al relé.
//  Con módulos activos en BAJO, "apagado" = pin en alta impedancia (INPUT).
//  Con módulos activos en ALTO, "apagado" = pin en LOW.
// ---------------------------------------------------------------------
static void releEscribir(uint8_t pin, bool on) {
  if (paradaOn) on = false;       // parada de emergencia: ningún camino puede encender un relé
#if RELE_ACTIVO_BAJO
  if (on) { pinMode(pin, OUTPUT); digitalWrite(pin, LOW); }
  else    { pinMode(pin, INPUT); }
#else
  pinMode(pin, OUTPUT);
  digitalWrite(pin, on ? HIGH : LOW);
#endif
}

// Segundos mínimos apagado de un relé de clima: el compresor nunca menos que FRIO_MIN_OFF_S.
static uint16_t minOffDe(uint8_t pin) {
  return (pin == PIN_RELE_FRIO && cfg.minOffS < FRIO_MIN_OFF_S) ? FRIO_MIN_OFF_S : cfg.minOffS;
}

// Cambia un relé respetando el tiempo mínimo en el estado anterior.
// "forzar" ignora los tiempos (se usa solo para apagar por seguridad).
static void aplicar(bool& actual, uint8_t pin, uint8_t ev, bool deseado, uint32_t& ultimoCambio, bool forzar, bool manual = false) {
  if (deseado == actual) return;
  if (deseado && paradaOn) return;                 // parada de emergencia: solo se apaga
  // REGLA INVIOLABLE: humidificador y deshumidificador NUNCA juntos. Por esta barrera pasan el automático, el forzado manual, la web, el LCD
  // y el hub: para prender uno, el otro tiene que estar apagado (controlForzar() lo apaga antes). Ver también controlVerificarExclusion().
  if (deseado && ((pin == PIN_RELE_HUM && est.releDesh) || (pin == PIN_RELE_DESH && est.releHum))) return;
  // Para encender hay que haber estado apagado minOffS; apagar es inmediato (no hay mínimo encendido desde la v1.0.39).
  uint32_t espera = deseado ? (uint32_t)minOffDe(pin) * 1000UL : 0;
  if (!forzar && (millis() - ultimoCambio) < espera) return;
  actual = deseado;
  ultimoCambio = millis();
  releEscribir(pin, deseado);
  eventosRegistrar(ev, deseado, manual);
}

// Cambio inmediato, sin tiempos mínimos (ventilador, luz y otros).
static void aplicarDirecto(bool& actual, uint8_t pin, uint8_t ev, bool deseado) {
  if (deseado == actual) return;
  if (deseado && paradaOn) return;                 // parada de emergencia: solo se apaga
  actual = deseado;
  releEscribir(pin, deseado);
  eventosRegistrar(ev, deseado);
}

// Como constrain(), pero un valor inválido guardado (NaN) vuelve al valor de fábrica en vez de trabar el control.
static float limitar(float v, float lo, float hi, float def) { return !isfinite(v) ? def : (v < lo ? lo : (v > hi ? hi : v)); }

uint8_t controlPeriodoValido(int horas) {
  uint8_t mejor = PERIODOS_VENT[0];
  for (uint8_t p : PERIODOS_VENT) {
    if (abs(horas - (int)p) < abs(horas - (int)mejor)) mejor = p;
  }
  return mejor;
}

void controlInit() {
#ifdef ARDUINO_ARCH_ESP32
  if (!candado) candado = xSemaphoreCreateRecursiveMutex();
#endif
  for (uint8_t i = 0; i < TEST_RELES; i++) releEscribir(PIN6[i], false);

  prefs.begin("camara", false);
  cfg.humSP           = prefs.getFloat("humSP",     DEF_HUM_SP);
  // Histéresis superior e inferior independientes. Si la clave nueva no existe (versión anterior), se parte de la única que había.
  const float humVieja  = prefs.getFloat("humHist",  DEF_HUM_HIST);
  const float tempVieja = prefs.getFloat("tempHist", DEF_TEMP_HIST);
  cfg.humHistSup      = prefs.isKey("humHistSup")  ? prefs.getFloat("humHistSup",  humVieja)  : humVieja;
  cfg.humHistInf      = prefs.isKey("humHistInf")  ? prefs.getFloat("humHistInf",  humVieja)  : humVieja;
  cfg.tempSP          = prefs.getFloat("tempSP",    DEF_TEMP_SP);
  cfg.tempHistSup     = prefs.isKey("tempHistSup") ? prefs.getFloat("tempHistSup", tempVieja) : tempVieja;
  cfg.tempHistInf     = prefs.getFloat("tempHistInf", DEF_TEMP_HIST_INF);
  cfg.humEsperaS      = prefs.getUShort("humEsp",   DEF_ESPERA_HUM_S);
  cfg.deshEsperaS     = prefs.getUShort("deshEsp",  DEF_ESPERA_HUM_S);
  cfg.humOff          = prefs.getBool("humOff",     false);
  perfilCargado       = prefs.getUChar("perfCarg",  0);
  if (perfilCargado > PERFILES_N) perfilCargado = 0;
  cfg.deshOff         = prefs.getBool("deshOff",    false);
  cfg.humPulsoS       = prefs.getUShort("humPul",   DEF_HUM_PULSO_S);
  cfg.humDescansoS    = prefs.getUShort("humDes",   DEF_HUM_DESC_S);
  cfg.humBloqueoS     = prefs.getUShort("humBlq",   DEF_HUM_BLOQ_S);
  cfg.humPromMin      = prefs.getUShort("humPro",   DEF_HUM_PROM_MIN);
  cfg.humPisoV        = prefs.getUShort("humPiso",  DEF_HUM_PISO);
  cfg.minOffS         = prefs.getUShort("minOffS",  DEF_MIN_OFF_S);
  cfg.ventActivo      = prefs.getBool("ventAct",    DEF_VENT_ACTIVO);
  cfg.ventPeriodoH    = prefs.getUChar("ventPer",   DEF_VENT_PERIODO_H);
  cfg.ventDuracionMin = prefs.getUShort("ventDur",  DEF_VENT_DURACION_MIN);
  cfg.ventAnclaMin    = prefs.getUShort("ventAnc",  DEF_VENT_ANCLA_MIN);
  cfg.activo          = prefs.getBool("activo",     true);
  paradaOn            = prefs.getBool("parada",     false);   // la parada de emergencia sobrevive a un reinicio
  cfg.manualMin       = prefs.getUShort("manMin",   DEF_MANUAL_MIN);
  cfg.offT            = limitar(prefs.getFloat("offT", 0.0f), -OFF_T_MAX, OFF_T_MAX, 0.0f);
  cfg.offH            = limitar(prefs.getFloat("offH", 0.0f), -OFF_H_MAX, OFF_H_MAX, 0.0f);
  controlNombreAux(0, prefs.getString("nomA1", "").c_str());
  controlNombreAux(1, prefs.getString("nomA2", "").c_str());

  // Todos los valores dentro de los rangos de los deslizables.
  cfg.humSP     = limitar(cfg.humSP,     HUM_SP_MIN,  HUM_SP_MAX, DEF_HUM_SP);
  cfg.humHistInf  = limitar(cfg.humHistInf,  HIST_HUM_MIN,  HIST_HUM_MAX,  DEF_HUM_HIST);
  cfg.humHistSup  = limitar(cfg.humHistSup,  HIST_HUM_MIN,  HIST_HUM_MAX,  DEF_HUM_HIST);
  cfg.tempSP    = limitar(cfg.tempSP,    TEMP_SP_MIN, TEMP_SP_MAX, DEF_TEMP_SP);
  cfg.tempHistSup = limitar(cfg.tempHistSup, HIST_TEMP_MIN, HIST_TEMP_MAX, DEF_TEMP_HIST);
  cfg.tempHistInf = limitar(cfg.tempHistInf, 0.0f,          HIST_TEMP_MAX, DEF_TEMP_HIST_INF);
  if (cfg.humEsperaS  > ESPERA_MAX_S) cfg.humEsperaS  = ESPERA_MAX_S;
  if (cfg.deshEsperaS > ESPERA_MAX_S) cfg.deshEsperaS = ESPERA_MAX_S;
  if (cfg.humPulsoS    > HUM_PULSO_MAX_S)  cfg.humPulsoS    = HUM_PULSO_MAX_S;
  if (cfg.humDescansoS > HUM_DESC_MAX_S)   cfg.humDescansoS = HUM_DESC_MAX_S;
  if (cfg.humBloqueoS  > HUM_BLOQ_MAX_S)   cfg.humBloqueoS  = HUM_BLOQ_MAX_S;
  if (cfg.humPromMin   > HUM_PROM_MAX_MIN) cfg.humPromMin   = HUM_PROM_MAX_MIN;
  if (cfg.humPisoV     > HUM_PISO_MAX)     cfg.humPisoV     = HUM_PISO_MAX;
  if (cfg.minOffS > 600) cfg.minOffS = 600;
  cfg.ventPeriodoH = controlPeriodoValido(cfg.ventPeriodoH);
  if (cfg.ventDuracionMin > VENT_DURACION_MAX) cfg.ventDuracionMin = VENT_DURACION_MAX;
  if (cfg.ventAnclaMin > 1439) cfg.ventAnclaMin = 0;
  if (cfg.manualMin < MANUAL_MIN_MIN) cfg.manualMin = MANUAL_MIN_MIN;
  if (cfg.manualMin > MANUAL_MIN_MAX) cfg.manualMin = MANUAL_MIN_MAX;

  // Tras un arranque se cuenta como si recién se hubiera apagado: espera minOffS
  // antes del primer encendido (protege el compresor ante cortes de luz cortos).
  cambioHum = cambioDesh = cambioFrio = millis();
}

void controlGuardarConfig() {
  prefs.putFloat("humSP",     cfg.humSP);
  prefs.putFloat("humHistInf",  cfg.humHistInf);
  prefs.putFloat("humHistSup",  cfg.humHistSup);
  prefs.putFloat("humHist",     cfg.humHistSup);    // clave anterior: si se vuelve a un programa viejo, queda un valor razonable
  prefs.putFloat("tempSP",    cfg.tempSP);
  prefs.putFloat("tempHistInf", cfg.tempHistInf);
  prefs.putFloat("tempHistSup", cfg.tempHistSup);
  prefs.putFloat("tempHist",    cfg.tempHistSup);
  prefs.putUShort("humEsp",   cfg.humEsperaS);
  prefs.putUShort("deshEsp",  cfg.deshEsperaS);
  prefs.putBool("humOff",     cfg.humOff);
  prefs.putBool("deshOff",    cfg.deshOff);
  prefs.putUShort("humPul",   cfg.humPulsoS);
  prefs.putUShort("humDes",   cfg.humDescansoS);
  prefs.putUShort("humBlq",   cfg.humBloqueoS);
  prefs.putUShort("humPro",   cfg.humPromMin);
  prefs.putUShort("humPiso",  cfg.humPisoV);
  prefs.putUShort("minOffS",  cfg.minOffS);
  prefs.putBool("ventAct",    cfg.ventActivo);
  prefs.putUChar("ventPer",   cfg.ventPeriodoH);
  prefs.putUShort("ventDur",  cfg.ventDuracionMin);
  prefs.putUShort("ventAnc",  cfg.ventAnclaMin);
  prefs.putBool("activo",     cfg.activo);
  prefs.putUShort("manMin",   cfg.manualMin);
  prefs.putFloat("offT",      cfg.offT);
  prefs.putFloat("offH",      cfg.offH);
  prefs.putString("nomA1",    cfg.nomAux1);
  prefs.putString("nomA2",    cfg.nomAux2);
}

// ---------------------------------------------------------------------
//  Perfiles de setpoints. Un blob por casillero ("p1".."p4") en el namespace "perfiles".
// ---------------------------------------------------------------------
static const char* const PERFIL_NOMBRE[PERFILES_N] = { "Estufado", "Secado 1", "Secado 2", "Otros" };

// Mismas letras que los nombres de AUX, máximo 12. Los cuatro nombres se pueden cambiar; vacío = el de fábrica.
static void limpiarNombrePerfil(char* dst, const char* src) {
  uint8_t n = 0;
  for (const char* p = src ? src : ""; *p && n < 12; p++) {
    const char c = *p;
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || strchr(" -./()+#", c)) dst[n++] = c;
  }
  while (n && dst[n - 1] == ' ') n--;
  dst[n] = 0;
}

static void perfilClave(uint8_t i, char* k) { k[0] = 'p'; k[1] = '1' + i; k[2] = 0; }

bool controlPerfilLeer(uint8_t i, Perfil& p) {
  memset(&p, 0, sizeof p);
  if (i >= PERFILES_N) return false;
  strcpy(p.nombre, PERFIL_NOMBRE[i]);
  char k[3]; perfilClave(i, k);
  Preferences pp;
  if (!pp.begin("perfiles", true)) return false;
  // Los campos de Perfil solo se pueden AGREGAR AL FINAL: un perfil guardado con una versión anterior (más corto) se lee igual,
  // con lo que falta en cero.
  Perfil g;
  memset(&g, 0, sizeof g);
  const size_t n = pp.getBytes(k, &g, sizeof g);
  pp.end();
  if (n == 0 || n > sizeof g) return false;
  p = g;
  if (!p.nombre[0]) strcpy(p.nombre, PERFIL_NOMBRE[i]);
  p.nombre[12] = 0;
  return p.lleno;
}

// Copia en RAM de los 4 casilleros: /api/estado pregunta por el perfil activo cada segundo y no debe leer la NVS cada vez.
static Perfil perfilCache[PERFILES_N];
static bool   perfilCacheOk = false;             // se invalida al escribir
static bool perfilEscribir(uint8_t i, const Perfil& p) {
  perfilCacheOk = false;
  char k[3]; perfilClave(i, k);
  Preferences pp;
  if (!pp.begin("perfiles", false)) return false;
  const bool ok = pp.putBytes(k, &p, sizeof p) == sizeof p;
  pp.end();
  return ok;
}

void controlPerfilActual(Perfil& p) {
  p.lleno = true;                                // el nombre no se toca
  p.humSP = cfg.humSP; p.humHistInf = cfg.humHistInf; p.humHistSup = cfg.humHistSup;
  p.tempSP = cfg.tempSP; p.tempHistInf = cfg.tempHistInf; p.tempHistSup = cfg.tempHistSup;
  p.humEsperaS = cfg.humEsperaS; p.deshEsperaS = cfg.deshEsperaS;
  p.ventActivo = cfg.ventActivo; p.ventPeriodoH = cfg.ventPeriodoH;
  p.ventDuracionMin = cfg.ventDuracionMin; p.ventAnclaMin = cfg.ventAnclaMin;
  p.v2 = 1; p.humOff = cfg.humOff; p.deshOff = cfg.deshOff;
  p.v3 = 1; p.humPulsoS = cfg.humPulsoS; p.humDescansoS = cfg.humDescansoS; p.humBloqueoS = cfg.humBloqueoS; p.humPromMin = cfg.humPromMin;
  p.v4 = 1; p.humPisoV = cfg.humPisoV;
}

bool controlPerfilGuardar(uint8_t i) {
  if (i >= PERFILES_N) return false;
  Perfil p;
  controlPerfilLeer(i, p);                       // conserva el nombre elegido
  controlPerfilActual(p);
  const bool ok = perfilEscribir(i, p);
  if (ok) { perfilCargado = i + 1; prefs.putUChar("perfCarg", perfilCargado); }   // ahora los ajustes actuales son los de este perfil
  return ok;
}

// Guarda valores editados a mano en el perfil SIN tocar la configuración de la cámara. Mismos rangos que al cargar.
bool controlPerfilEditar(uint8_t i, const Perfil& nuevo) {
  if (i >= PERFILES_N) return false;
  Perfil p = nuevo, g;
  controlPerfilLeer(i, g);
  strcpy(p.nombre, g.nombre);                    // el nombre se cambia aparte
  p.lleno = true;
  p.v2 = 1;
  p.v3 = 1; p.v4 = 1;
  p.humSP       = limitar(p.humSP,       HUM_SP_MIN,   HUM_SP_MAX,   DEF_HUM_SP);
  p.humHistInf  = limitar(p.humHistInf,  HIST_HUM_MIN,  HIST_HUM_MAX,  DEF_HUM_HIST);
  p.humHistSup  = limitar(p.humHistSup,  HIST_HUM_MIN,  HIST_HUM_MAX,  DEF_HUM_HIST);
  p.tempSP      = limitar(p.tempSP,      TEMP_SP_MIN,  TEMP_SP_MAX,  DEF_TEMP_SP);
  p.tempHistInf = limitar(p.tempHistInf, 0.0f,         HIST_TEMP_MAX, DEF_TEMP_HIST_INF);
  p.tempHistSup = limitar(p.tempHistSup, HIST_TEMP_MIN, HIST_TEMP_MAX, DEF_TEMP_HIST);
  if (p.humPulsoS    > HUM_PULSO_MAX_S)  p.humPulsoS    = HUM_PULSO_MAX_S;
  if (p.humDescansoS > HUM_DESC_MAX_S)   p.humDescansoS = HUM_DESC_MAX_S;
  if (p.humBloqueoS  > HUM_BLOQ_MAX_S)   p.humBloqueoS  = HUM_BLOQ_MAX_S;
  if (p.humPromMin   > HUM_PROM_MAX_MIN) p.humPromMin   = HUM_PROM_MAX_MIN;
  if (p.humPisoV     > HUM_PISO_MAX)     p.humPisoV     = HUM_PISO_MAX;
  if (p.humEsperaS  > ESPERA_MAX_S) p.humEsperaS  = ESPERA_MAX_S;
  if (p.deshEsperaS > ESPERA_MAX_S) p.deshEsperaS = ESPERA_MAX_S;
  p.ventPeriodoH    = controlPeriodoValido(p.ventPeriodoH);
  if (p.ventDuracionMin > VENT_DURACION_MAX) p.ventDuracionMin = VENT_DURACION_MAX;
  if (p.ventAnclaMin > 1439) p.ventAnclaMin = 0;
  return perfilEscribir(i, p);
}

bool controlPerfilRenombrar(uint8_t i, const char* nombre) {
  if (i >= PERFILES_N) return false;
  Perfil p;
  controlPerfilLeer(i, p);
  limpiarNombrePerfil(p.nombre, nombre);
  if (!p.nombre[0]) strcpy(p.nombre, PERFIL_NOMBRE[i]);
  return perfilEscribir(i, p);
}

bool controlPerfilCargar(uint8_t i) {
  Perfil p;
  if (!controlPerfilLeer(i, p)) return false;
  cfg.humSP       = limitar(p.humSP,       HUM_SP_MIN,   HUM_SP_MAX,   cfg.humSP);
  cfg.humHistInf  = limitar(p.humHistInf,  HIST_HUM_MIN,  HIST_HUM_MAX,  cfg.humHistInf);
  cfg.humHistSup  = limitar(p.humHistSup,  HIST_HUM_MIN,  HIST_HUM_MAX,  cfg.humHistSup);
  cfg.tempSP      = limitar(p.tempSP,      TEMP_SP_MIN,  TEMP_SP_MAX,  cfg.tempSP);
  cfg.tempHistInf = limitar(p.tempHistInf, 0.0f,         HIST_TEMP_MAX, cfg.tempHistInf);
  cfg.tempHistSup = limitar(p.tempHistSup, HIST_TEMP_MIN, HIST_TEMP_MAX, cfg.tempHistSup);
  cfg.humEsperaS  = p.humEsperaS  > ESPERA_MAX_S ? ESPERA_MAX_S : p.humEsperaS;
  cfg.deshEsperaS = p.deshEsperaS > ESPERA_MAX_S ? ESPERA_MAX_S : p.deshEsperaS;
  cfg.ventActivo      = p.ventActivo;
  cfg.ventPeriodoH    = controlPeriodoValido(p.ventPeriodoH);
  cfg.ventDuracionMin = p.ventDuracionMin > VENT_DURACION_MAX ? VENT_DURACION_MAX : p.ventDuracionMin;
  cfg.ventAnclaMin    = p.ventAnclaMin > 1439 ? 0 : p.ventAnclaMin;
  cfg.humOff  = p.v2 ? p.humOff  : false;      // un perfil anterior a la v1.0.31 deja los dos equipos encendidos
  cfg.deshOff = p.v2 ? p.deshOff : false;
  if (p.v3) {                                    // un perfil anterior a la v1.0.39 deja esos cuatro ajustes como están
    cfg.humPulsoS    = p.humPulsoS    > HUM_PULSO_MAX_S  ? HUM_PULSO_MAX_S  : p.humPulsoS;
    cfg.humDescansoS = p.humDescansoS > HUM_DESC_MAX_S   ? HUM_DESC_MAX_S   : p.humDescansoS;
    cfg.humBloqueoS  = p.humBloqueoS  > HUM_BLOQ_MAX_S   ? HUM_BLOQ_MAX_S   : p.humBloqueoS;
    cfg.humPromMin   = p.humPromMin   > HUM_PROM_MAX_MIN ? HUM_PROM_MAX_MIN : p.humPromMin;
  }
  if (p.v4) cfg.humPisoV = p.humPisoV > HUM_PISO_MAX ? HUM_PISO_MAX : p.humPisoV;
  humReiniciar();                                // otros objetivos: el promedio y la tanda arrancan de cero
  pideHumDesde = pideDeshDesde = 0;              // los objetivos cambiaron: la espera de confirmación empieza de cero
  perfilCargado = i + 1;
  prefs.putUChar("perfCarg", perfilCargado);
  controlGuardarConfig();
  return true;
}

bool controlPerfilEtapa(char* nombre, size_t n, bool& modificado) {
  modificado = false;
  const uint8_t i = controlPerfilActivo();            // también deja cargada la copia en RAM
  if (i) { snprintf(nombre, n, "%s", perfilCache[i - 1].nombre); return true; }
  if (perfilCargado && perfilCargado <= PERFILES_N && perfilCache[perfilCargado - 1].lleno) {
    modificado = true;                                  // se cambió algo después de cargarlo
    snprintf(nombre, n, "%s", perfilCache[perfilCargado - 1].nombre);
    return true;
  }
  return false;
}

bool controlPerfilCopia(uint8_t i, Perfil& p) {
  memset(&p, 0, sizeof p);
  if (i >= PERFILES_N) return false;
  controlPerfilActivo();                              // deja cargada la copia en RAM
  p = perfilCache[i];
  return p.lleno;
}

uint8_t controlPerfilActivo() {
  auto igual = [](float a, float b) { return fabsf(a - b) < 0.05f; };
  if (!perfilCacheOk) {
    for (uint8_t i = 0; i < PERFILES_N; i++) controlPerfilLeer(i, perfilCache[i]);
    perfilCacheOk = true;
  }
  for (uint8_t i = 0; i < PERFILES_N; i++) {
    const Perfil& p = perfilCache[i];
    if (!p.lleno) continue;
    if (igual(p.humSP, cfg.humSP) && igual(p.humHistInf, cfg.humHistInf) && igual(p.humHistSup, cfg.humHistSup) &&
        igual(p.tempSP, cfg.tempSP) && igual(p.tempHistInf, cfg.tempHistInf) && igual(p.tempHistSup, cfg.tempHistSup) &&
        p.humEsperaS == cfg.humEsperaS && p.deshEsperaS == cfg.deshEsperaS &&
        p.ventActivo == cfg.ventActivo && p.ventPeriodoH == cfg.ventPeriodoH &&
        (p.v2 ? p.humOff : false) == cfg.humOff && (p.v2 ? p.deshOff : false) == cfg.deshOff &&
        (!p.v3 || (p.humPulsoS == cfg.humPulsoS && p.humDescansoS == cfg.humDescansoS && p.humBloqueoS == cfg.humBloqueoS && p.humPromMin == cfg.humPromMin)) &&
        (!p.v4 || p.humPisoV == cfg.humPisoV) &&
        p.ventDuracionMin == cfg.ventDuracionMin && p.ventAnclaMin == cfg.ventAnclaMin) return i + 1;
  }
  return 0;
}

// Red de seguridad de la regla inviolable (humidificador y deshumidificador NUNCA juntos). La barrera de aplicar() debería hacerla
// imposible; si por un error de programa aparecieran los dos prendidos, se apagan los dos enseguida. Se llama en cada vuelta del loop().
void controlVerificarExclusion() {
  if (est.releHum && est.releDesh) {
    aplicar(est.releHum,  PIN_RELE_HUM,  EV_HUM,  false, cambioHum,  true);
    aplicar(est.releDesh, PIN_RELE_DESH, EV_DESH, false, cambioDesh, true);
    Serial.println("SEGURIDAD: humidificador y deshumidificador juntos; se apagaron los dos");
  }
}

void controlNombreAux(uint8_t cual, const char* nombre) {
  char* dst = cual ? cfg.nomAux2 : cfg.nomAux1;
  char b[13];
  uint8_t n = 0;
  for (const char* p = nombre ? nombre : ""; *p && n < 12; p++) {
    const char c = *p;
    const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || strchr(" -./()+#", c);
    if (ok && !(c == ' ' && (n == 0 || b[n - 1] == ' '))) b[n++] = c;   // sin espacios al principio ni dobles
  }
  while (n && b[n - 1] == ' ') n--;
  b[n] = 0;
  snprintf(dst, 13, "%s", n ? b : (cual ? "AUX2" : "AUX1"));
}

// Durante el test de relés AUX1 y AUX2 no se tocan: se recuerda el pedido y se aplica al terminar.
void controlSetLuz(bool on) {
  if (paradaOn && on) return;                      // parada de emergencia: solo se puede apagar
  if (testOn) { luzPrev = on; return; }
  aplicarDirecto(est.releLuz, PIN_RELE_LUZ, EV_LUZ, on);
}

void controlSetOtros(bool on) {
  if (paradaOn && on) return;
  if (testOn) { otrosPrev = on; return; }
  aplicarDirecto(est.releOtros, PIN_RELE_OTROS, EV_OTROS, on);
}

// ---------------------------------------------------------------------
//  Forzado manual de los relés de clima (menú del equipo y web).
//  Dura cfg.manualMin minutos y después el relé vuelve solo a automático.
//  No se guarda en memoria: un reinicio vuelve todo a automático.
// ---------------------------------------------------------------------
// true si ese forzado va contra un límite absoluto (hardware.h, SEG_*).
static bool contraSeguridad(uint8_t cual, uint8_t modo) {
  if (modo == MAN_ON)  return (cual == R_FRIO && segFrioNo) || (cual == R_HUM && segHumNo) || (cual == R_DESH && segDeshNo);
  if (modo == MAN_OFF) return cual == R_FRIO && segCalor;
  return false;
}

static uint32_t esperaEncender(uint8_t cual) {   // segundos hasta poder encender (0 = ya se puede)
  if (cual > 2 || *ESTADO[cual]) return 0;
  const uint32_t espera = (uint32_t)minOffDe(PIN[cual]) * 1000UL;
  const uint32_t pasado = millis() - *CAMBIO[cual];
  return pasado >= espera ? 0 : (espera - pasado + 999) / 1000;
}

static const char* motivoRechazo = "";
const char* controlForzarMotivo() { return motivoRechazo; }

static uint32_t rechazar(const char* motivo) { motivoRechazo = motivo; return FORZAR_RECHAZADO; }

uint32_t controlForzar(uint8_t cual, uint8_t modo) {
  if (cual > 2 || modo > MAN_OFF) return 0;
  // Durante el test, una actualización o la calibración no se fuerza nada (volver a automático sí).
  if (testOn) return rechazar("test");
  if (otaOn)  return rechazar("ota");
  if (paradaOn && modo != MAN_AUTO) return rechazar("parada");
  if (modo != MAN_AUTO && calibActivo()) return rechazar("calibracion");
  if (contraSeguridad(cual, modo)) return rechazar("seguridad");   // un límite absoluto lo impide: no se toca nada

  if (modo == MAN_AUTO) {
    est.manual[cual] = MAN_AUTO;
    est.manualSeg[cual] = 0;
    return 0;
  }

  if (modo == MAN_ON) {
    const uint32_t esp = esperaEncender(cual);
    if (esp) return esp;                         // todavía no se puede encender: no se toca nada
    if (cual != R_FRIO) {
      // Humidificador y deshumidificador nunca juntos: el opuesto se apaga y vuelve a automático.
      const uint8_t otro = (cual == R_HUM) ? R_DESH : R_HUM;
      est.manual[otro] = MAN_AUTO;
      est.manualSeg[otro] = 0;
      aplicar(*ESTADO[otro], PIN[otro], EVC[otro], false, *CAMBIO[otro], true, true);
    }
    aplicar(*ESTADO[cual], PIN[cual], EVC[cual], true, *CAMBIO[cual], true, true);
  } else {
    aplicar(*ESTADO[cual], PIN[cual], EVC[cual], false, *CAMBIO[cual], true, true);   // apagar es inmediato
  }

  manualFin[cual] = millis() + (uint32_t)cfg.manualMin * 60000UL;   // primero el vencimiento, después el modo
  est.manualSeg[cual] = (int32_t)cfg.manualMin * 60;
  est.manual[cual] = modo;
  return 0;
}

void controlManualLoop() {
  const uint32_t ahora = millis();
  for (uint8_t i = 0; i < 3; i++) {
    if (est.manual[i] == MAN_AUTO) { est.manualSeg[i] = 0; continue; }
    const int32_t resta = (int32_t)(manualFin[i] - ahora);        // seguro ante el desborde de millis()
    if (resta <= 0) { est.manual[i] = MAN_AUTO; est.manualSeg[i] = 0; }
    else            est.manualSeg[i] = (resta + 999) / 1000;
  }
}

// Ventilador: se enciende "ventDuracionMin" minutos cada "ventPeriodoH" horas,
// contando desde la hora del día "ventAnclaMin". Usa la hora de internet; si todavía
// no la tiene, usa el tiempo desde el arranque para no dejar la cámara sin ventilar.
static void ventilador() {
  est.ventSeg = -1;
  bool quiere = false;

  if (cfg.activo && cfg.ventActivo && cfg.ventDuracionMin > 0) {
    const int64_t P = (int64_t)cfg.ventPeriodoH * 3600;
    const int64_t D = (int64_t)cfg.ventDuracionMin * 60;
    const int64_t A = (int64_t)cfg.ventAnclaMin * 60;

    const time_t ahora = time(nullptr);
    int64_t t;
    if (ahora > 1700000000) {
      t = (int64_t)ahora + GMT_OFFSET_S;     // segundos en hora local
    } else {
      t = segDesdeArranque();
    }

    int64_t rel = (t - A) % P;
    if (rel < 0) rel += P;

    if (D >= P)      { quiere = true; est.ventSeg = 0; }              // encendido continuo
    else if (rel < D) { quiere = true; est.ventSeg = (int32_t)(D - rel); }
    else              {                est.ventSeg = (int32_t)(P - rel); }
  }
  aplicarDirecto(est.releVent, PIN_RELE_VENT, EV_VENT, quiere);
}

static SensorDiag sensorDiag;
static uint8_t     masRelesDiag = 0;     // relés prendidos en la lectura anterior (bit por relé)
static uint32_t    ultConmDiag = 0;      // millis() del último cambio de algún relé
const SensorDiag& controlSensorDiag() { return sensorDiag; }

static uint32_t horaRealDiag() { const time_t t = time(nullptr); return t > 1700000000 ? (uint32_t)t : 0; }

void controlActualizar(float t, float h, bool ok) {
  const uint32_t ahora = millis();

  // Diagnóstico del sensor
  const uint8_t reles = (est.releHum ? 1 : 0) | (est.releDesh ? 2 : 0) | (est.releFrio ? 4 : 0) | (est.releVent ? 8 : 0) | (est.releLuz ? 16 : 0) | (est.releOtros ? 32 : 0);
  if (reles != masRelesDiag) { masRelesDiag = reles; ultConmDiag = ahora; }
  sensorDiag.lecturas++;
  if (ok) {
    if (sensorDiag.seguidos > sensorDiag.maxSeguidos) sensorDiag.maxSeguidos = sensorDiag.seguidos;
    sensorDiag.seguidos = 0;
    sensorDiag.ultOkMs = ahora;
    sensorDiag.ultOkHora = horaRealDiag();
  } else {
    sensorDiag.errores++;
    sensorDiag.seguidos++;
    if (sensorDiag.seguidos > sensorDiag.maxSeguidos) sensorDiag.maxSeguidos = sensorDiag.seguidos;
    if (ultConmDiag && (ahora - ultConmDiag) < 6000UL) sensorDiag.errTrasRele++;
  }

  if (ok) {
    est.temp = t;
    est.hum  = h;
    est.sensorOk = true;
    ultimaLecturaOk = ahora;
  } else {
    if (est.sensorOk && (ahora - ultimaLecturaOk) > TIMEOUT_SENSOR_MS) {
      est.sensorOk = false;
      sensorDiag.caidas++;
      sensorDiag.ultCaidaMs = ahora;
      sensorDiag.ultCaidaHora = horaRealDiag();
    }
  }

  if (otaOn && millis() - otaIni > OTA_MAX_MS) controlOta(false);   // la carga quedó colgada
  if (testOn || otaOn || paradaOn) {         // test de relés, actualización o parada de emergencia: el automático no toca nada
    pideHumDesde = pideDeshDesde = 0;
    humReiniciar();
    return;
  }

  ventilador();   // no depende del sensor; solo se detiene con la pausa

  // Límites absolutos con la lectura SIN ajuste: no dependen de la consigna, los offsets ni la pausa.
  // Con el sensor caído o en el frasco de calibración no hay lectura de la cámara: no se aplican.
  const bool hayCruda = est.sensorOk && !calibActivo() && isfinite(est.tempCruda) && isfinite(est.humCruda);
  if (!hayCruda)                                  segCalor = false;
  else if (est.tempCruda > SEG_T_MAX)              segCalor = true;
  else if (est.tempCruda < SEG_T_MAX - SEG_T_HIST) segCalor = false;
  segFrioNo = hayCruda && est.tempCruda < SEG_T_MIN;
  segHumNo  = hayCruda && est.humCruda  > SEG_HR_MAX;
  segDeshNo = hayCruda && est.humCruda  < SEG_HR_MIN;
  // Un forzado manual que va contra un límite se cancela (vuelve a automático).
  for (uint8_t i = 0; i < 3; i++) {
    if (est.manual[i] != MAN_AUTO && contraSeguridad(i, est.manual[i])) {
      est.manual[i] = MAN_AUTO;
      est.manualSeg[i] = 0;
      Serial.printf("SEGURIDAD: forzado manual %u cancelado (T=%.1f HR=%.1f sin ajuste)\n", (unsigned)i, est.tempCruda, est.humCruda);
    }
  }

  // Un relé forzado a mano no lo toca el automático (ni la pausa, ni el sensor caído).
  const bool aHum  = est.manual[R_HUM]  == MAN_AUTO;
  const bool aDesh = est.manual[R_DESH] == MAN_AUTO;
  const bool aFrio = est.manual[R_FRIO] == MAN_AUTO;

  if (!cfg.activo || !est.sensorOk || calibActivo()) {
    // Pausa manual, sensor muerto o sensor en el frasco de calibración: clima apagado, sin esperar tiempos mínimos.
    // Excepción: en pausa, si hace demasiado calor el frío enciende igual (respetando el mínimo apagado).
    pideHumDesde = pideDeshDesde = 0;   // la espera de confirmación empieza de cero al volver
    humReiniciar();                     // y el promedio lento también (si no, un promedio viejo podría prender el humidificador al volver)
    if (aHum) aplicar(est.releHum,  PIN_RELE_HUM,  EV_HUM,  false, cambioHum,  true);
    if (aDesh) aplicar(est.releDesh, PIN_RELE_DESH, EV_DESH, false, cambioDesh, true);
    if (aFrio) aplicar(est.releFrio, PIN_RELE_FRIO, EV_FRIO, segCalor, cambioFrio, !segCalor);
  } else if (ok) {
    // ---------------- Humedad ----------------
    //   humidificador    ON si HR < SP - Hinf  OFF si HR >= SP
    //   deshumidificador ON si HR > SP + Hsup  OFF si HR <= SP
    const float hr = est.hum;
    bool quiereHum  = est.releHum;
    bool quiereDesh = est.releDesh;

    // Pasar el límite no alcanza para prender: tiene que seguir pasado durante la espera de confirmación
    // (humEsperaS / deshEsperaS), así una caída o subida de segundos no lo dispara. Una vez prendido, sigue
    // hasta el objetivo como siempre. Un forzado manual reinicia la cuenta.
    const float umbralHum = cfg.humSP - cfg.humHistInf;
    const bool bajaHum = hr < umbralHum;
    const bool altaHum = hr > cfg.humSP + cfg.humHistSup;

    // ---- Humidificador (v1.0.39): bloqueo después del frío, promedio lento, tanda de pulsos y descanso ----
    // Con el compresor prendido la humedad cae rápido y al apagarse rebota: esa humedad no dice nada del producto. Mientras el frío está
    // prendido, y hasta humBloqueoS después de apagarse, no se empieza a humidificar, el promedio no se mueve y el tiempo no se cuenta.
    // Válvula (piso ajustable, humPisoV): si la humedad cae por debajo del piso durante el bloqueo, da UN pulso corto de chequeo (con su descanso) sin reiniciar ni frenar
    // el contador de la tanda. Además un bloqueo nunca dura más de 30 min seguidos.
    const float piso = (float)cfg.humPisoV;     // 0 = sin válvula
    const bool enBloq = cfg.humBloqueoS > 0 && (est.releFrio || (ahora - cambioFrio) < (uint32_t)cfg.humBloqueoS * 1000UL);
    bool bloq = false;
    if (enBloq) { if (!bloqDesde) bloqDesde = ahora | 1; bloq = (ahora - bloqDesde) < BLOQ_MAX_MS; } else bloqDesde = 0;
    const bool valvula = enBloq && cfg.humPisoV > 0 && hr < piso && aHum && !cfg.humOff;
    const bool promOn = cfg.humPromMin > 0;
    if (promOn != promPrev) { promPrev = promOn; promOk = false; bajoAcumMs = 0; tanda = false; }   // se prendió o apagó el promedio desde los ajustes
    if (!promOn || !aHum || cfg.humOff) {
      promOk = false; bajoAcumMs = 0; est.humProm = NAN; est.humBajoMin = 0;
    } else if (!promOk) {                      // arranque (o vuelta de una pausa): parte de la lectura actual y espera unos minutos antes de decidir
      promHum = hr; promOk = true; promUlt = promIni = ahora; bajoAcumMs = 0;
    } else {
      uint32_t dt = ahora - promUlt; promUlt = ahora;
      if (dt > 60000UL) dt = 60000UL;
      if (!enBloq) {
        promHum += (1.0f - expf(-(float)dt / 1000.0f / PROM_TAU_S)) * (hr - promHum);
        const uint32_t calent = (uint32_t)(cfg.humPromMin < 10 ? cfg.humPromMin : 10) * 60000UL;
        if (promHum >= umbralHum) bajoAcumMs = 0;
        else if (ahora - promIni >= calent) bajoAcumMs += dt;
      }
      est.humProm = promHum; est.humBajoMin = (uint16_t)(bajoAcumMs / 60000UL);
    }
    const bool promBajo = promOn && promOk && promHum < umbralHum;

    if (!bajaHum || !aHum || cfg.humOff || bloq || promOn) pideHumDesde = 0; else if (!pideHumDesde) pideHumDesde = ahora | 1;
    if (!altaHum || !aDesh || cfg.deshOff)  pideDeshDesde = 0; else if (!pideDeshDesde) pideDeshDesde = ahora | 1;
    const bool confHum  = pideHumDesde  && (ahora - pideHumDesde)  >= (uint32_t)cfg.humEsperaS  * 1000UL;
    const bool confDesh = pideDeshDesde && (ahora - pideDeshDesde) >= (uint32_t)cfg.deshEsperaS * 1000UL;

    if (!aHum || cfg.humOff) {
      tanda = false; valv = false;
    } else if (est.releHum && valv) {          // pulso de la válvula: termina por tiempo (o al volver al límite) y NO toca la tanda ni el contador
      if (hr >= umbralHum || (cfg.humPulsoS > 0 && (ahora - cambioHum) >= (uint32_t)cfg.humPulsoS * 1000UL)) { quiereHum = false; valv = false; }
    } else if (est.releHum) {                  // prendido: hasta el objetivo, o hasta que termine el pulso
      if (hr >= cfg.humSP) { quiereHum = false; tanda = false; bajoAcumMs = 0; }
      else if (cfg.humPulsoS > 0 && (ahora - cambioHum) >= (uint32_t)cfg.humPulsoS * 1000UL) quiereHum = false;   // fin del pulso: ahora se evalúa el promedio
    } else {                                   // apagado: ¿empieza o sigue la tanda?
      if (tanda && (hr >= cfg.humSP || (promOn && !promBajo))) { tanda = false; bajoAcumMs = 0; }   // el promedio se recuperó: se termina la tanda
      const bool empieza = promOn ? (promOk && bajoAcumMs >= (uint32_t)cfg.humPromMin * 60000UL) : confHum;
      const bool descanso = cfg.humDescansoS > 0 && (ahora - cambioHum) < (uint32_t)cfg.humDescansoS * 1000UL;
      if ((tanda || empieza) && !descanso && !bloq && hr < cfg.humSP) { quiereHum = true; tanda = true; pideHumDesde = 0; }
      else if (valvula && !descanso && hr < cfg.humSP) { quiereHum = true; valv = true; }   // chequeo: un pulso, sin tocar tanda ni contador
      else quiereHum = false;
    }
    est.humFase = (!aHum || cfg.humOff) ? 0 : est.releHum ? ((cfg.humPulsoS > 0 || valv) ? 3 : 0) : bloq ? 1 : (cfg.humDescansoS > 0 && (ahora - cambioHum) < (uint32_t)cfg.humDescansoS * 1000UL && tanda) ? 2 : tanda ? 4 : 0;

    if      (confDesh)               quiereDesh = true;
    else if (hr <= cfg.humSP)        quiereDesh = false;

    if (segHumNo)  quiereHum  = false;
    if (segDeshNo) quiereDesh = false;
    if (cfg.humOff)  quiereHum  = false;   // apagado a pedido (perfil o ajustes): no prende solo; si estaba prendido, se apaga enseguida
    if (cfg.deshOff) quiereDesh = false;

    // Primero se apaga, después se enciende; nunca los dos a la vez.
    if (aHum  && !quiereHum)  aplicar(est.releHum,  PIN_RELE_HUM,  EV_HUM,  false, cambioHum,  false);
    if (aDesh && !quiereDesh) aplicar(est.releDesh, PIN_RELE_DESH, EV_DESH, false, cambioDesh, false);
    if (aHum  && quiereHum  && !est.releDesh) aplicar(est.releHum,  PIN_RELE_HUM,  EV_HUM,  true, cambioHum,  false);
    if (aDesh && quiereDesh && !est.releHum)  aplicar(est.releDesh, PIN_RELE_DESH, EV_DESH, true, cambioDesh, false);

    // ---------------- Temperatura ----------------
    //   frío: ON si T > SP + Hsup     OFF si T <= SP - Hinf (Hinf = 0: apaga en el objetivo)
    if (aFrio) {
      const float tt = est.temp;
      bool quiereFrio = est.releFrio;
      if      (tt >  cfg.tempSP + cfg.tempHistSup) quiereFrio = true;
      else if (tt <= cfg.tempSP - cfg.tempHistInf) quiereFrio = false;
      if (segCalor)  quiereFrio = true;
      if (segFrioNo) quiereFrio = false;
      aplicar(est.releFrio, PIN_RELE_FRIO, EV_FRIO, quiereFrio, cambioFrio, segFrioNo);   // demasiado frío: apaga sin esperar
    }
  }
}

// =====================================================================
//  MODO TEST DE RELÉS
// =====================================================================
#define TEST_PASO_MS   (TEST_ON_MS + TEST_PAUSA_MS)
#define TEST_TOTAL_MS  (TEST_PAUSA_MS + TEST_RELES * TEST_PASO_MS)   // de a uno: ~25 s

const char* controlNombreRele(uint8_t i) {
  if (i == 4) return cfg.nomAux1;
  if (i == 5) return cfg.nomAux2;
  return i < TEST_RELES ? NOMBRE6[i] : "";
}

// Función conectada al relé k+1 (orden físico del módulo).
uint8_t controlTestOrden(uint8_t k) {
  for (uint8_t f = 0; f < TEST_RELES; f++) if (numeroRele(PIN6[f]) == k + 1) return f;
  return k < TEST_RELES ? k : 0;
}
bool    controlTestActivo()  { return testOn; }
uint8_t controlTestModo()    { return testOn ? testModo : (uint8_t)TEST_NINGUNO; }
uint8_t controlTestMascara() { return testOn ? testMask : 0; }

static uint8_t encendidos(uint8_t m) { uint8_t n = 0; for (; m; m &= m - 1) n++; return n; }

int8_t controlTestRele() {
  if (!testOn || encendidos(testMask) != 1) return -1;
  for (int8_t i = 0; i < TEST_RELES; i++) if (testMask & (1 << i)) return i;
  return -1;
}

// Milisegundos desde el primer relé (negativo durante la pausa inicial).
static int32_t testTranscurrido() { return (int32_t)(millis() - testIni) - (int32_t)TEST_PAUSA_MS; }
static uint32_t testDuracion() { return testModo == TEST_UNO ? TEST_TOTAL_MS : TEST_PAUSA_MS + TEST_LARGO_MS; }

// Funciones que tienen que estar encendidas a "t" ms del primer relé (bit i = función i).
static uint8_t testMascaraEn(int32_t t) {
  if (t < 0) return 0;
  if (testModo == TEST_UNO)
    return ((uint32_t)t % TEST_PASO_MS) < TEST_ON_MS ? (uint8_t)(1 << controlTestOrden((uint8_t)((uint32_t)t / TEST_PASO_MS))) : 0;
  uint32_t n = testModo == TEST_SUMA ? (uint32_t)t / TEST_SUMA_MS + 1 : TEST_RELES;   // sumar: en orden físico, de IN1 a IN6
  if (n > TEST_RELES) n = TEST_RELES;
  uint8_t m = 0;
  for (uint8_t k = 0; k < n; k++) m |= (uint8_t)(1 << controlTestOrden(k));
  return m;
}

uint8_t controlTestNumero() {
  if (!testOn) return 0;
  if (testModo != TEST_UNO) return encendidos(testMask);
  const int32_t t = testTranscurrido();
  if (t < 0) return 1;
  const uint32_t k = (uint32_t)t / TEST_PASO_MS + 1;
  return k > TEST_RELES ? TEST_RELES : (uint8_t)k;
}

uint8_t controlTestHechos() {
  if (!testOn) return 0;
  if (testModo != TEST_UNO) return encendidos(testMask);
  const int32_t t = testTranscurrido();
  if (t < 0) return 0;
  const uint32_t k = (uint32_t)t / TEST_PASO_MS + (((uint32_t)t % TEST_PASO_MS) >= TEST_ON_MS ? 1 : 0);
  return k > TEST_RELES ? TEST_RELES : (uint8_t)k;
}

uint16_t controlTestSegRestantes() {
  if (!testOn) return 0;
  const uint32_t pasado = millis() - testIni, total = testDuracion();
  return pasado >= total ? 0 : (uint16_t)((total - pasado + 999) / 1000);
}

static void testTodoApagado() {
  for (uint8_t i = 0; i < TEST_RELES; i++) { releEscribir(PIN6[i], false); *EST6[i] = false; }
  testMask = 0;
}

static void testIniciar(uint8_t modo) {
  if (testOn || otaOn || paradaOn) return;
  // Los forzados manuales se cancelan y el clima y el ventilador se apagan con su evento normal.
  for (uint8_t i = 0; i < 3; i++) { est.manual[i] = MAN_AUTO; est.manualSeg[i] = 0; }
  aplicar(est.releHum,  PIN_RELE_HUM,  EV_HUM,  false, cambioHum,  true);
  aplicar(est.releDesh, PIN_RELE_DESH, EV_DESH, false, cambioDesh, true);
  aplicar(est.releFrio, PIN_RELE_FRIO, EV_FRIO, false, cambioFrio, true);
  aplicarDirecto(est.releVent, PIN_RELE_VENT, EV_VENT, false);
  luzPrev = est.releLuz;          // AUX1 y AUX2 se restituyen al terminar
  otrosPrev = est.releOtros;
  testTodoApagado();
  eventosRegistrar(EV_TEST, true, true);
  testModo = modo;
  testOn = true;
  testIni = millis();
  Serial.printf("TEST DE RELES: inicio (modo %u)\n", (unsigned)modo);
}

static void testTerminar() {
  if (!testOn) return;
  testTodoApagado();
  testOn = false;
  // El clima espera el tiempo mínimo apagado antes de volver a encender (por si quedó
  // conectado el compresor). El ventilador lo vuelve a manejar su programa.
  cambioHum = cambioDesh = cambioFrio = millis();
  const bool l = luzPrev && !paradaOn, o = otrosPrev && !paradaOn;   // en parada no se restituye nada encendido
  releEscribir(PIN_RELE_LUZ,   l); est.releLuz   = l;
  releEscribir(PIN_RELE_OTROS, o); est.releOtros = o;
  eventosRegistrar(EV_TEST, false, true);
  Serial.println("TEST DE RELES: fin");
}

uint32_t controlTestPedirModo(uint8_t modo) {
  if (modo == TEST_NINGUNO) { testPedido = -1; return 0; }
  if (modo > TEST_TODOS || (!TEST_AVANZADO && modo != TEST_UNO)) return 0;
  testPedido = (int8_t)modo;
  return 0;
}

void controlTestLoop() {
  const int8_t p = testPedido;
  if (p) {
    testPedido = 0;
    if (p > 0) testIniciar((uint8_t)p); else testTerminar();
  }
  if (!testOn) return;
  if (paradaOn || millis() - testIni >= testDuracion()) { testTerminar(); return; }

  const uint8_t quiere = testMascaraEn(testTranscurrido());
  if (quiere == testMask) return;
  for (uint8_t i = 0; i < TEST_RELES; i++)                     // primero se apaga lo que sobra...
    if ((testMask & ~quiere) & (1 << i)) { releEscribir(PIN6[i], false); *EST6[i] = false; }
  for (uint8_t i = 0; i < TEST_RELES; i++)                     // ...y después se enciende lo nuevo
    if ((quiere & ~testMask) & (1 << i)) { releEscribir(PIN6[i], true); *EST6[i] = true; }
  testMask = quiere;
  char b[20] = "";
  for (uint8_t k = 0; k < TEST_RELES; k++) if (testMask & (1 << controlTestOrden(k))) snprintf(b + strlen(b), sizeof(b) - strlen(b), " %u", (unsigned)(k + 1));
  Serial.printf("TEST: encendidos:%s\n", testMask ? b : " ninguno");
}

// =====================================================================
//  ACTUALIZACIÓN POR WI-FI (OTA)
//  Mientras se graba el programa nuevo, el clima y el ventilador quedan apagados y el automático
//  suspendido (como en el test de relés). Si sale bien, el equipo se reinicia y al arrancar el clima
//  espera el tiempo mínimo apagado; si falla, el control vuelve solo. AUX1 y AUX2 no se tocan.
//  Desde la web, llamar con ControlCandado.
// =====================================================================
void controlOta(bool on) {
  if (on == otaOn) return;
  if (on) {
    if (testOn) testTerminar();
    for (uint8_t i = 0; i < 3; i++) { est.manual[i] = MAN_AUTO; est.manualSeg[i] = 0; }
    aplicar(est.releHum,  PIN_RELE_HUM,  EV_HUM,  false, cambioHum,  true);
    aplicar(est.releDesh, PIN_RELE_DESH, EV_DESH, false, cambioDesh, true);
    aplicar(est.releFrio, PIN_RELE_FRIO, EV_FRIO, false, cambioFrio, true);
    aplicarDirecto(est.releVent, PIN_RELE_VENT, EV_VENT, false);
    otaIni = millis();
    otaOn = true;
    Serial.println("OTA: clima apagado mientras se graba el programa");
  } else {
    otaOn = false;
    cambioHum = cambioDesh = cambioFrio = millis();   // vuelve contando el tiempo mínimo apagado
    Serial.println("OTA: el control vuelve a funcionar");
  }
}

bool controlOtaActiva() { return otaOn; }

// =====================================================================
//  PARADA DE EMERGENCIA ("APAGAR TODO")
//  Apaga los 6 relés (también el frío y los AUX) y deja el control suspendido: no hay clima, ventilador
//  ni protección de temperatura hasta reanudar. Se guarda en NVS: un reinicio, una actualización o un
//  corte de luz NO la levantan. No toca la pausa del control (cfg.activo), que es otra cosa.
//  Orden: primero la bandera (desde ahí releEscribir() solo puede apagar), después el resto.
//  Desde la web, llamar con ControlCandado.
// =====================================================================
void controlParada(bool on) {
  if (on == paradaOn) return;
  if (on) {
    paradaOn = true;
    prefs.putBool("parada", true);
    if (testOn) testTerminar();
    for (uint8_t i = 0; i < 3; i++) { est.manual[i] = MAN_AUTO; est.manualSeg[i] = 0; }
    aplicar(est.releHum,  PIN_RELE_HUM,  EV_HUM,  false, cambioHum,  true);
    aplicar(est.releDesh, PIN_RELE_DESH, EV_DESH, false, cambioDesh, true);
    aplicar(est.releFrio, PIN_RELE_FRIO, EV_FRIO, false, cambioFrio, true);
    aplicarDirecto(est.releVent,  PIN_RELE_VENT,  EV_VENT,  false);
    aplicarDirecto(est.releLuz,   PIN_RELE_LUZ,   EV_LUZ,   false);
    aplicarDirecto(est.releOtros, PIN_RELE_OTROS, EV_OTROS, false);
    luzPrev = otrosPrev = false;
    eventosRegistrar(EV_PARADA, true, true);
    Serial.println("PARADA DE EMERGENCIA: todo apagado");
  } else {
    paradaOn = false;
    prefs.putBool("parada", false);
    cambioHum = cambioDesh = cambioFrio = millis();   // el clima espera el tiempo mínimo apagado
    eventosRegistrar(EV_PARADA, false, true);
    Serial.println("PARADA DE EMERGENCIA: levantada, el control vuelve");
  }
}

bool controlParadaActiva() { return paradaOn; }
