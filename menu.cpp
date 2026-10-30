#include "menu.h"
#include "control.h"
#include "eventos.h"
#include "encoder.h"
#include "hardware.h"
#include "redwifi.h"
#include "calibracion.h"
#include "config.h"
#include "balanza.h"
#include "pesaje.h"

#define TIMEOUT_MENU_MS  20000UL
#define TIMEOUT_BAL_MS   300000UL   // la pantalla de la balanza se puede dejar mirando 5 min

enum : uint8_t { M_OFF, M_LISTA, M_EDIT, M_CONF, M_MSG, M_INFO, M_WIFI, M_TEST, M_CAL, M_BAL, M_PARADA };
// Tipos de entrada. Los primeros son valores editables; los últimos son submenús o acciones.
enum : uint8_t { T_TEMP, T_HUM, T_HTEMP, T_HHUM, T_VACT, T_VPER, T_VDUR, T_LUZ, T_OTROS, T_PAUSA,
                 T_FHUM, T_FDESH, T_FFRIO, T_MANMIN, T_MINON, T_MINONT, T_BANDA, T_OFFT, T_OFFH,
                 T_SUB_TEMP, T_SUB_HUM, T_SUB_VENT, T_IP, T_SUB_CAL, T_VOLVER, T_SALIR, T_VERIP, T_WIFI, T_TEST, T_CALIB,
                 T_TSUMA, T_TTODOS, T_ACERCA, T_SUB_TIEMPOS, T_MINOFF, T_SUB_TEST, T_TUNO,
                 T_SUB_BAL, T_BVER, T_BBLE, T_BESTADO, T_BRECON, T_BOLV, T_BSEG, T_PARADA, T_N };

struct Item { const char* corto; const char* largo; };
static const Item ITEMS[T_N] = {
  { "Objetivo",   "Temperatura objetivo" },
  { "Objetivo",   "Humedad objetivo" },
  { "Histeresis", "Histeresis temp." },
  { "Histeresis", "Histeresis humedad" },
  { "Programa",   "Programa ventilador" },
  { "Cada",       "Ventilador: cada" },
  { "Duracion",   "Ventilador: duracion" },
  { "AUX1",       "AUX1" },        // se reemplaza por el nombre elegido en la web
  { "AUX2",       "AUX2" },
  { "Control",    "Control automatico" },
  { "Forzar hum.", "Forzar humidificador" },
  { "Forzar desh", "Forzar deshumidific." },
  { "Forzar frio", "Forzar refrigeracion" },
  { "Dur forzado","Duracion del forzado" },
  { "Min encend.","Min encendido humed." },
  { "Min encend.","Min encendido frio" },
  { "Alarma +/-", "Alarma temp. +/-" },
  { "Ajuste T",   "Ajuste temperatura" },
  { "Ajuste H",   "Ajuste humedad" },
  { "Temperatura","Temperatura" },
  { "Humedad",    "Humedad" },
  { "Ventilador", "Ventilador" },
  { "Diagnostico","Diagnostico" },
  { "Calibracion","Calibracion" },
  { "Volver",     "Volver" },
  { "Salir",      "Salir del menu" },
  { "Red e IP",   "Red e IP" },
  { "Config WiFi","Configurar Wi-Fi" },
  { "Test reles", "Test de reles" },
  { "Calibr. sal","Calibrar con sal" },
  { "Sumar 1 a 6", "Sumar reles 1 a 6" },
  { "Todos juntos", "Los 6 reles juntos" },
  { "Acerca de",  "Acerca de" },
  { "Tiempos",    "Tiempos" },
  { "Min apagado","Min apagado (clima)" },
  { "Test reles", "Test de reles" },
  { "De a uno",   "Test de a uno" },
  { "Balanza",    "Balanza" },
  { "Peso/merma", "Peso y merma" },
  { "Conexion",   "Conexion balanza" },
  { "Estado red", "Estado de la conexion" },
  { "Buscar",     "Buscar la balanza" },
  { "Olvidar IP", "Olvidar la balanza" },
  { "Seguimiento","Seguimiento merma" },
  { "APAGAR TODO","APAGAR TODO" },          // cambia a REANUDAR mientras esté la parada (ver corto/largo)
};

// Nombre a mostrar: AUX1 y AUX2 usan el nombre elegido en la web.
static const char* corto(uint8_t t) { return t == T_LUZ ? cfg.nomAux1 : t == T_OTROS ? cfg.nomAux2 : (t == T_PARADA && controlParadaActiva()) ? "REANUDAR TODO" : ITEMS[t].corto; }
static const char* largo(uint8_t t) { return t == T_LUZ ? cfg.nomAux1 : t == T_OTROS ? cfg.nomAux2 : (t == T_PARADA && controlParadaActiva()) ? "REANUDAR TODO" : ITEMS[t].largo; }

// Listas del menú: la principal y los submenús. El forzado manual de cada relé de clima está
// dentro de su categoría (refrigeración en Temperatura; humidificador y deshumidificador en Humedad);
// lo que vale para los tres relés de clima (tiempo mínimo apagado, duración del forzado) va en Tiempos.
struct Lista { const char* titulo; const uint8_t* it; uint8_t n; uint8_t padre; };   // padre: adónde lleva "Volver"
enum : uint8_t { LI_PRINC, LI_TEMP, LI_HUM, LI_VENT, LI_DIAG, LI_CAL, LI_TIEMPOS, LI_TEST, LI_BAL, LI_N };
#if USAR_BALANZA
static const uint8_t L_PRINC[] = { T_SUB_TEMP, T_SUB_HUM, T_SUB_VENT, T_LUZ, T_OTROS, T_PAUSA,
                                   T_SUB_BAL, T_SUB_TIEMPOS, T_SUB_CAL, T_IP, T_PARADA, T_SALIR };
#else
static const uint8_t L_PRINC[] = { T_SUB_TEMP, T_SUB_HUM, T_SUB_VENT, T_LUZ, T_OTROS, T_PAUSA,
                                   T_SUB_TIEMPOS, T_SUB_CAL, T_IP, T_PARADA, T_SALIR };
#endif
static const uint8_t L_TEMP[]  = { T_TEMP, T_HTEMP, T_BANDA, T_MINONT, T_FFRIO, T_VOLVER };
static const uint8_t L_HUM[]   = { T_HUM, T_HHUM, T_MINON, T_FHUM, T_FDESH, T_VOLVER };
static const uint8_t L_VENT[]  = { T_VACT, T_VPER, T_VDUR, T_VOLVER };
#if TEST_AVANZADO
static const uint8_t L_DIAG[]  = { T_VERIP, T_WIFI, T_SUB_TEST, T_ACERCA, T_VOLVER };   // test: submenú con los 3 modos
#else
static const uint8_t L_DIAG[]  = { T_VERIP, T_WIFI, T_TEST, T_ACERCA, T_VOLVER };       // test: solo de a uno, directo
#endif
static const uint8_t L_CAL[]   = { T_OFFT, T_OFFH, T_CALIB, T_VOLVER };
static const uint8_t L_TIEMPOS[] = { T_MINOFF, T_MANMIN, T_VOLVER };
static const uint8_t L_TEST[]  = { T_TUNO, T_TSUMA, T_TTODOS, T_VOLVER };
static const uint8_t L_BAL[]   = { T_BVER, T_BSEG, T_BBLE, T_BESTADO, T_BRECON, T_BOLV, T_VOLVER };
static const Lista LISTAS[LI_N] = {
  { "MENU",        L_PRINC,   sizeof(L_PRINC),   LI_PRINC },
  { "TEMPERATURA", L_TEMP,    sizeof(L_TEMP),    LI_PRINC },
  { "HUMEDAD",     L_HUM,     sizeof(L_HUM),     LI_PRINC },
  { "VENTILADOR",  L_VENT,    sizeof(L_VENT),    LI_PRINC },
  { "DIAGNOSTICO", L_DIAG,    sizeof(L_DIAG),    LI_PRINC },
  { "CALIBRACION", L_CAL,     sizeof(L_CAL),     LI_PRINC },
  { "TIEMPOS",     L_TIEMPOS, sizeof(L_TIEMPOS), LI_PRINC },
  { "TEST RELES",  L_TEST,    sizeof(L_TEST),    LI_DIAG },
  { "BALANZA",     L_BAL,     sizeof(L_BAL),     LI_PRINC },
};

// Opciones que abren otra lista (llevan ">" a la derecha).
static bool esSubmenu(uint8_t t) {
  return t == T_SUB_TEMP || t == T_SUB_HUM || t == T_SUB_VENT || t == T_IP || t == T_SUB_CAL || t == T_SUB_TIEMPOS || t == T_SUB_TEST || t == T_SUB_BAL;
}
// Opciones que abren una pantalla o hacen algo (no tienen un valor que mostrar).
static bool sinValor(uint8_t t) {
  return t == T_SALIR || t == T_VOLVER || t == T_VERIP || t == T_WIFI || t == T_ACERCA ||
         t == T_TEST || t == T_TUNO || t == T_TSUMA || t == T_TTODOS ||
         t == T_BVER || t == T_BESTADO || t == T_BRECON || t == T_BOLV || t == T_PARADA;
}
// Acciones inmediatas (no se guardan como ajuste): la confirmación dice "Aplicar".
static bool esAccion(uint8_t t) { return t == T_LUZ || t == T_OTROS || t == T_FHUM || t == T_FDESH || t == T_FFRIO || t == T_BBLE || t == T_BSEG; }

static const char* const ETQ_ONOFF[] = { "OFF", "ON" };
static const char* const ETQ_VACT[]  = { "Apagado", "Activo" };
static const char* const ETQ_PAUSA[] = { "Activo", "PAUSA" };
static const char* const ETQ_SEG[]   = { "Sigue", "PAUSA" };   // seguimiento de la pieza testigo
static const char* const ETQ_FORZ[]  = { "Auto", "ON", "OFF" };
static const char* const ETQ_PER[]   = { "1 h", "2 h", "3 h", "4 h", "6 h", "8 h", "12 h", "24 h" };

struct Campo {
  bool  lista;                  // true = se elige entre etiquetas; false = número
  float min, max, paso;
  uint8_t dec;
  const char* uni;
  uint8_t n;
  const char* const* et;
};

static Campo campo(uint8_t t) {
  switch (t) {
    case T_TEMP:   return { false, TEMP_SP_MIN, TEMP_SP_MAX, 0.5f, 1, "C",   0, nullptr };
    case T_HUM:    return { false, HUM_SP_MIN,  HUM_SP_MAX,  0.5f, 1, "%",   0, nullptr };
    case T_HTEMP:  return { false, 0.5f, 5.0f,  0.5f, 1, "C",   0, nullptr };
    case T_HHUM:   return { false, 0.5f, 10.0f, 0.5f, 1, "%",   0, nullptr };
    case T_BANDA:  return { false, 1.0f, 10.0f, 0.5f, 1, "C",   0, nullptr };
    case T_OFFT:   return { false, -OFF_T_MAX, OFF_T_MAX, 0.1f, 1, "C", 0, nullptr };
    case T_OFFH:   return { false, -OFF_H_MAX, OFF_H_MAX, 0.1f, 1, "%", 0, nullptr };
    case T_VDUR:   return { false, 0, VENT_DURACION_MAX, 1, 0, "min", 0, nullptr };
    case T_MINON:
    case T_MINONT: return { false, 0, 600, 10, 0, "s", 0, nullptr };
    case T_MANMIN: return { false, MANUAL_MIN_MIN, MANUAL_MIN_MAX, 5, 0, "min", 0, nullptr };
    case T_MINOFF: return { false, 0, 600, 10, 0, "s", 0, nullptr };
    case T_VACT:   return { true, 0, 1, 1, 0, "", 2, ETQ_VACT };
    case T_VPER:   return { true, 0, 7, 1, 0, "", 8, ETQ_PER };
    case T_LUZ:
    case T_OTROS:
    case T_BBLE:   return { true, 0, 1, 1, 0, "", 2, ETQ_ONOFF };
    case T_PAUSA:  return { true, 0, 1, 1, 0, "", 2, ETQ_PAUSA };
    case T_BSEG:   return { true, 0, 1, 1, 0, "", 2, ETQ_SEG };
    default:       return { true, 0, 2, 1, 0, "", 3, ETQ_FORZ };   // T_FHUM, T_FDESH, T_FFRIO
  }
}

static float valorActual(uint8_t t) {
  switch (t) {
    case T_TEMP:   return cfg.tempSP;
    case T_HUM:    return cfg.humSP;
    case T_HTEMP:  return cfg.tempHist;
    case T_HHUM:   return cfg.humHist;
    case T_BANDA:  return cfg.tempBanda;
    case T_OFFT:   return cfg.offT;
    case T_OFFH:   return cfg.offH;
    case T_VACT:   return cfg.ventActivo ? 1 : 0;
    case T_VPER: {
      for (uint8_t i = 0; i < 8; i++) if (PERIODOS_VENT[i] == cfg.ventPeriodoH) return i;
      return 0;
    }
    case T_VDUR:   return cfg.ventDuracionMin;
    case T_LUZ:    return est.releLuz ? 1 : 0;
    case T_OTROS:  return est.releOtros ? 1 : 0;
    case T_PAUSA:  return cfg.activo ? 0 : 1;
    case T_FHUM:   return est.manual[R_HUM];
    case T_FDESH:  return est.manual[R_DESH];
    case T_FFRIO:  return est.manual[R_FRIO];
    case T_MANMIN: return cfg.manualMin;
    case T_MINON:  return cfg.minOnS;
    case T_MINONT: return cfg.minOnTempS;
    case T_MINOFF: return cfg.minOffS;
#if USAR_BALANZA
    case T_BBLE:   return balanzaEstado().activo ? 1 : 0;
    case T_BSEG:   return pesajeEstado().pausado ? 1 : 0;
#endif
    default:       return 0;
  }
}

static void textoValor(uint8_t t, float v, char* b, size_t n) {
  const Campo c = campo(t);
  if (c.lista) {
    int i = (int)(v + 0.5f);
    if (i < 0) i = 0;
    if (i >= c.n) i = c.n - 1;
    snprintf(b, n, "%s", c.et[i]);
  } else if (t == T_OFFT || t == T_OFFH) {
    snprintf(b, n, "%+.1f%s", fabsf(v) < 0.05f ? 0.0f : v, c.uni);   // ajustes con signo
  } else {
    snprintf(b, n, "%.*f%s", c.dec, v, c.uni);
  }
}

// Aplica el valor confirmado. Devuelve 0 si salió bien o los segundos de espera si un
// encendido forzado todavía no está permitido (tiempo mínimo apagado).
static uint32_t aplicarValor(uint8_t t, float v) {
  const int i = (int)(v + 0.5f);
  switch (t) {
    case T_TEMP:   cfg.tempSP = v; break;
    case T_HUM:    cfg.humSP = v; break;
    case T_HTEMP:  cfg.tempHist = v; break;
    case T_HHUM:   cfg.humHist = v; break;
    case T_BANDA:  cfg.tempBanda = v; break;
    case T_OFFT:   cfg.offT = v; break;
    case T_OFFH:   cfg.offH = v; break;
    case T_VACT:   cfg.ventActivo = (i != 0); break;
    case T_VPER:   cfg.ventPeriodoH = PERIODOS_VENT[i < 0 ? 0 : (i > 7 ? 7 : i)]; break;
    case T_VDUR:   cfg.ventDuracionMin = (uint16_t)(v + 0.5f); break;
    case T_LUZ:    controlSetLuz(i != 0); return 0;
    case T_OTROS:  controlSetOtros(i != 0); return 0;
#if USAR_BALANZA
    case T_BBLE:   balanzaActivar(i != 0); return 0;
    case T_BSEG:   pesajePausar(i != 0); return 0;
#endif
    case T_PAUSA: {
      const bool antes = cfg.activo;
      cfg.activo = (i == 0);
      if (cfg.activo != antes) eventosRegistrar(EV_PAUSA, !cfg.activo);
      break;
    }
    case T_FHUM:   return controlForzar(R_HUM,  (uint8_t)i);
    case T_FDESH:  return controlForzar(R_DESH, (uint8_t)i);
    case T_FFRIO:  return controlForzar(R_FRIO, (uint8_t)i);
    case T_MANMIN: cfg.manualMin = (uint16_t)(v + 0.5f); break;
    case T_MINON:  cfg.minOnS     = (uint16_t)(v + 0.5f); break;
    case T_MINONT: cfg.minOnTempS = (uint16_t)(v + 0.5f); break;
    case T_MINOFF: cfg.minOffS    = (uint16_t)(v + 0.5f); break;
    default: return 0;
  }
  controlGuardarConfig();
  return 0;
}

// ---------------------------------------------------------------------
static uint8_t  estado = M_OFF;
static uint8_t  lista = LI_PRINC;          // lista que se está viendo (LI_*)
static uint8_t  cursores[LI_N] = {};       // posición recordada en cada lista
static uint8_t  sel = 0;
static float    editVal = 0;
static bool     wifiConf = false;      // config Wi-Fi: true = pantalla de confirmación, false = instrucciones
static uint8_t  calPaso = 0;          // calibración: 2 confirmar inicio, 10 estado, 11 opciones, 12 aplicar, 13 cancelar
static uint8_t  calOp = 0;
static bool     msgACal = false;      // al cerrar el mensaje, volver a la pantalla de calibración
static uint8_t  paradaPaso = 0;        // parada de emergencia: 1 advertencia, 2 ultima confirmacion, 3 reanudar
static uint8_t  testPaso = 0;         // test de relés: 1 y 2 = advertencias, 3 = corriendo
static bool     infoAcerca = false;       // M_INFO muestra "Acerca de" en vez de la red
static uint8_t  testModoMenu = TEST_UNO;   // qué test se eligió en Diagnóstico
static bool     testVisto = false;    // ya se vio el test activo (para saber cuándo terminó)
static bool     confSi = false;       // selección actual del SI/NO (siempre arranca en NO)
static bool     volverOff = false;    // true si se entró por el atajo de pausa
static uint8_t  balPant = 0;         // balanza: 0 peso y merma, 1 estado de la conexión, 2 confirmar "olvidar"
static uint32_t ultAct = 0, msgHasta = 0, ultGiro = 0;
static char     msgTxt[2][21];

// Señal Wi-Fi estable: promedio exponencial lento, se actualiza una vez por segundo y
// se muestra redondeada a pasos de 5 dBm junto con una barra de 4 niveles.
static float rssiSuave = 0;
static bool  rssiInicio = true;
static uint32_t rssiUlt = 0;
int menuRssiFiltrado(int crudo, uint32_t ahora) {
  if (rssiInicio) { rssiSuave = (float)crudo; rssiInicio = false; rssiUlt = ahora; }
  else if (ahora - rssiUlt >= 1000UL) {
    rssiUlt = ahora;
    rssiSuave += 0.15f * ((float)crudo - rssiSuave);
  }
  return (int)(roundf(rssiSuave / 5.0f) * 5.0f);
}
static void textoSenal(char* o, size_t n, int dbm) {
  const char* calidad = dbm >= -60 ? "Excelente" : dbm >= -70 ? "Buena" : dbm >= -80 ? "Regular" : "Debil";
  snprintf(o, n, "%ddBm %s", dbm, calidad);
}

#ifdef ARDUINO_ARCH_ESP32
#include <WiFi.h>
static void datosRed(char* ip, size_t n, char* senal, size_t m) {
  if (WiFi.status() == WL_CONNECTED) {
    snprintf(ip, n, "%s", WiFi.localIP().toString().c_str());
    textoSenal(senal, m, menuRssiFiltrado((int)WiFi.RSSI(), millis()));
  } else {
    snprintf(ip, n, "sin conexion");
    snprintf(senal, m, "Sin Wi-Fi");
    rssiInicio = true;
  }
}
#else
static void datosRed(char* ip, size_t n, char* senal, size_t m) { snprintf(ip, n, "192.168.1.50"); textoSenal(senal, m, menuRssiFiltrado(-60, 0)); }
#endif

void menuInit() { estado = M_OFF; }
bool menuActivo() { return estado != M_OFF; }

static void mostrarMensaje(const char* a, const char* b, uint32_t ahora, uint32_t ms) {
  snprintf(msgTxt[0], sizeof(msgTxt[0]), "%s", a);
  snprintf(msgTxt[1], sizeof(msgTxt[1]), "%s", b);
  msgHasta = ahora + ms;
  estado = M_MSG;
}

static void salirDeConfirmacion() { estado = volverOff ? M_OFF : M_LISTA; }

void menuEntrada(int16_t giro, uint8_t boton, uint32_t ahora) {
  if (giro || boton) ultAct = ahora;
  if (controlTestActivo()) ultAct = ahora;            // durante el test de relés el menú no se cierra solo

  const uint32_t limite = (estado == M_BAL && balPant < 2) ? TIMEOUT_BAL_MS : TIMEOUT_MENU_MS;
  if (estado != M_OFF && estado != M_MSG && ahora - ultAct > limite) {
    estado = M_OFF;                                   // 20 s sin tocar: se descarta todo
    return;
  }

  switch (estado) {
    case M_OFF:
      if (controlTestActivo()) {                      // test iniciado desde la web: un click lo detiene
        if (boton) controlTestPedirModo(TEST_NINGUNO);
        break;
      }
      if (boton == BTN_CLICK) { estado = M_LISTA; lista = LI_PRINC; volverOff = false; ultAct = ahora; memset(cursores, 0, sizeof(cursores)); }
      else if (boton == BTN_LARGO) {                  // atajo: pausar / reanudar con confirmación
        sel = T_PAUSA;
        editVal = cfg.activo ? 1 : 0;                 // el valor opuesto al actual
        confSi = false;
        volverOff = true;
        estado = M_CONF;
        ultAct = ahora;
      }
      break;

    case M_LISTA: {
      const Lista& L = LISTAS[lista];
      uint8_t& cur = cursores[lista];
      if (giro) cur = (uint8_t)((((int)cur + giro) % L.n + L.n) % L.n);
      if (boton == BTN_LARGO) { estado = M_OFF; break; }
      if (boton == BTN_CLICK) {
        const uint8_t t = L.it[cur];
        auto abrir = [&](uint8_t li) { lista = li; cursores[li] = 0; };
        if      (t == T_SALIR)    estado = M_OFF;
        else if (t == T_VOLVER)   lista = L.padre;               // vuelve a la lista de arriba, donde estaba
        else if (t == T_SUB_TEMP) abrir(LI_TEMP);
        else if (t == T_SUB_HUM)  abrir(LI_HUM);
        else if (t == T_SUB_VENT) abrir(LI_VENT);
        else if (t == T_IP)       abrir(LI_DIAG);
        else if (t == T_SUB_CAL)  abrir(LI_CAL);
        else if (t == T_SUB_TIEMPOS) abrir(LI_TIEMPOS);
        else if (t == T_SUB_TEST) abrir(LI_TEST);
        else if (t == T_SUB_BAL)  abrir(LI_BAL);
        else if (t == T_BVER)     { balPant = 0; estado = M_BAL; }
        else if (t == T_BESTADO)  { balPant = 1; estado = M_BAL; }
        else if (t == T_BOLV)     { balPant = 2; confSi = false; estado = M_BAL; }
#if USAR_BALANZA
        else if (t == T_BSEG && !pesajeEstado().activo) { volverOff = false; mostrarMensaje("Sin seguimiento", "Empezar en la web", ahora, 2000); }
#endif
#if USAR_BALANZA
        else if (t == T_BRECON)   { balanzaReconectar(); volverOff = false; mostrarMensaje("Buscando la", "balanza en la red", ahora, 1500); }
#endif
        else if (t == T_PARADA)   { paradaPaso = controlParadaActiva() ? 3 : 1; confSi = false; estado = M_PARADA; }
        else if (t == T_CALIB)    { calPaso = calibActivo() ? 10 : 2; confSi = false; estado = M_CAL; }
        else if (t == T_VERIP)    { rssiInicio = true; infoAcerca = false; estado = M_INFO; }
        else if (t == T_ACERCA)   { infoAcerca = true; estado = M_INFO; }
        else if (t == T_WIFI)     { wifiConf = !wifiEnConfig(); confSi = false; estado = M_WIFI; }   // si ya está activo, muestra las instrucciones
        else if (t == T_TEST || t == T_TUNO || t == T_TSUMA || t == T_TTODOS) {
          testModoMenu = t == T_TSUMA ? TEST_SUMA : t == T_TTODOS ? TEST_TODOS : TEST_UNO;
          testPaso = 1; confSi = false; estado = M_TEST;
        }
        else { sel = t; editVal = valorActual(sel); estado = M_EDIT; }
      }
      break;
    }

    case M_WIFI:
      if (wifiConf) {                                 // ¿crear la red de configuración? (empieza en NO)
        if (giro) confSi = !confSi;
        if (boton == BTN_LARGO || (boton == BTN_CLICK && !confSi)) { estado = M_LISTA; break; }
        if (boton == BTN_CLICK) { wifiIniciarConfig(); wifiConf = false; }
      } else {                                        // instrucciones: click = volver, mantener = cerrar
        if (boton == BTN_CLICK) estado = M_LISTA;
        else if (boton == BTN_LARGO) {
          wifiCerrarConfig();
          volverOff = false;
          mostrarMensaje("Config WiFi cerrada", "", ahora, 1500);
        }
      }
      break;

    case M_INFO:
      if (boton) estado = M_LISTA;
      break;

    case M_BAL:
      if (balPant == 2) {                             // ¿olvidar la balanza vinculada? (empieza en NO)
        if (giro) confSi = !confSi;
        if (boton == BTN_LARGO || (boton == BTN_CLICK && !confSi)) { estado = M_LISTA; break; }
        if (boton == BTN_CLICK) {
#if USAR_BALANZA
          balanzaOlvidar();
#endif
          volverOff = false;
          mostrarMensaje("Balanza olvidada", "buscando de nuevo", ahora, 2000);
        }
      } else {                                        // girar = peso <-> conexión, click = volver
        if (giro) balPant ^= 1;
        if (boton) estado = M_LISTA;
      }
      break;

    case M_CAL:
      if (calPaso == 2 || calPaso == 12 || calPaso == 13) {   // confirmaciones SI/NO
        if (giro) confSi = !confSi;
        if (boton == BTN_LARGO || (boton == BTN_CLICK && !confSi)) { if (calPaso == 2) estado = M_LISTA; else calPaso = 10; break; }
        if (boton == BTN_CLICK) {
          volverOff = false;
          if (calPaso == 2) { calibIniciar(); calPaso = 10; }
          else if (calPaso == 12) {
            if (calibAplicar()) { char b[21]; snprintf(b, sizeof(b), "Ajuste H %+.1f%%", cfg.offH); mostrarMensaje("Calibracion lista", b, ahora, 2500); }
            else calPaso = 10;
          } else { calibCancelar(); mostrarMensaje("Calibracion", "cancelada", ahora, 1500); }
        }
      } else if (calPaso == 10) {                     // estado: click = opciones, mantener = volver (sigue calibrando)
        if (!calibActivo()) { estado = M_LISTA; break; }
        if (boton == BTN_LARGO) { estado = M_LISTA; break; }
        if (boton == BTN_CLICK || giro) { calPaso = 11; calOp = 0; }
      } else if (calPaso == 11) {                     // opciones: aplicar / cancelar / volver
        if (giro) calOp = (uint8_t)(((int)calOp + giro % 3 + 3) % 3);
        if (boton == BTN_LARGO) { calPaso = 10; break; }
        if (boton == BTN_CLICK) {
          if (calOp == 0) {
            if (calibEstado().sePuede) { calPaso = 12; confSi = false; }
            else { volverOff = false; mostrarMensaje("Todavia no:", "minimo 60 minutos", ahora, 2000); msgACal = true; }
          } else if (calOp == 1) { calPaso = 13; confSi = false; }
          else estado = M_LISTA;
        }
      }
      break;

    case M_PARADA:                                      // el cursor siempre arranca en NO
      if (giro) confSi = !confSi;
      if (boton == BTN_LARGO) { estado = M_LISTA; break; }
      if (boton == BTN_CLICK) {
        if (!confSi) { estado = M_LISTA; break; }
        if (paradaPaso == 1) { paradaPaso = 2; confSi = false; break; }
        volverOff = false;
        if (paradaPaso == 2) { controlParada(true);  mostrarMensaje("TODO APAGADO", "Reanudar: Menu", ahora, 3000); }
        else                 { controlParada(false); mostrarMensaje("Control reanudado", "Clima automatico", ahora, 2500); }
      }
      break;

    case M_TEST:
      if (testPaso < 3) {                             // dos advertencias, cada una empieza en NO
        if (giro) confSi = !confSi;
        if (boton == BTN_LARGO) { estado = M_LISTA; break; }
        if (boton == BTN_CLICK) {
          if (!confSi) { estado = M_LISTA; break; }
          if (testPaso == 1) { testPaso = 2; confSi = false; break; }
          const uint32_t espera = controlTestPedirModo(testModoMenu);
          if (espera) {                                // la heladera estuvo apagada poco tiempo
            char b[21];
            snprintf(b, sizeof(b), "esperar %lu:%02lu", (unsigned long)(espera / 60), (unsigned long)(espera % 60));
            volverOff = false;
            mostrarMensaje("Heladera: proteccion", b, ahora, 3000);
            break;
          }
          testPaso = 3;
          testVisto = false;
        }
      } else {
        if (controlTestActivo()) testVisto = true;
        if (boton) controlTestPedirModo(TEST_NINGUNO);           // click o pulsación larga: detener
        if (testVisto && !controlTestActivo()) {
          volverOff = false;
          mostrarMensaje("Test terminado", "Todo en automatico", ahora, 2500);
        }
      }
      break;

    case M_EDIT: {
      const Campo c = campo(sel);
      if (giro) {
        if (c.lista) {
          int i = (((int)(editVal + 0.5f) + giro) % c.n + c.n) % c.n;
          editVal = (float)i;
        } else {
          // Girando rápido avanza de a 5 pasos.
          const int mult = (ahora - ultGiro < 90) ? 5 : 1;
          editVal += giro * c.paso * mult;
          editVal = roundf(editVal / c.paso) * c.paso;
          if (editVal < c.min) editVal = c.min;
          if (editVal > c.max) editVal = c.max;
        }
        ultGiro = ahora;
      }
      if (boton == BTN_LARGO) { estado = M_LISTA; break; }
      if (boton == BTN_CLICK) {
        if (fabsf(editVal - valorActual(sel)) < 0.001f) estado = M_LISTA;   // no cambió nada
        else { confSi = false; estado = M_CONF; }
      }
      break;
    }

    case M_CONF:
      if (giro) confSi = !confSi;
      if (boton == BTN_LARGO) { salirDeConfirmacion(); break; }
      if (boton == BTN_CLICK) {
        if (!confSi) { salirDeConfirmacion(); break; }
        const uint32_t espera = aplicarValor(sel, editVal);
        if (espera == FORZAR_RECHAZADO) {
          mostrarMensaje("No se puede ahora", controlForzarMotivo(), ahora, 2500);
        } else if (espera) {
          char b[21];
          snprintf(b, sizeof(b), "Espera %lu:%02lu", (unsigned long)(espera / 60), (unsigned long)(espera % 60));
          mostrarMensaje("No se puede aun", b, ahora, 2500);
        } else {
          mostrarMensaje(esAccion(sel) ? "Listo" : "Guardado", "", ahora, 900);
        }
      }
      break;

    case M_MSG:
      if ((int32_t)(ahora - msgHasta) >= 0 || boton) {   // resta: seguro ante el desborde de millis()
        salirDeConfirmacion();
        if (msgACal) { msgACal = false; estado = M_CAL; calPaso = 10; }
        ultAct = ahora;
      }
      break;
  }
}

void menuLoop() {
  menuEntrada(encoderGiro(), encoderBoton(), millis());
}

// ---------------------------------------------------------------------
// Renglón de confirmación SI/NO (el cursor arranca en NO).
static const char* siNo(uint8_t filas) {
  if (filas >= 4) return confSi ? "     NO      [SI]" : "    [NO]      SI";
  return confSi ? "   NO     [SI]" : "  [NO]     SI";
}

void menuLineas(char l[4][21], uint8_t filas) {
  for (int i = 0; i < 4; i++) l[i][0] = 0;
  char act[16], nue[16], b[24];

  switch (estado) {
    case M_LISTA: {
      const Lista& L = LISTAS[lista];
      const uint8_t cur = cursores[lista];
      auto valor = [&](uint8_t t, char* o, size_t n) {
        if (esSubmenu(t))                  snprintf(o, n, ">");
        else if (t == T_CALIB)             snprintf(o, n, "%s", calibActivo() ? "ACTIVA" : "");
        else if (sinValor(t))              o[0] = 0;          // Volver, Red e IP, test, Acerca de...
        else                               textoValor(t, valorActual(t), o, n);
      };
      if (filas >= 4) {
        snprintf(l[0], 21, "%-15s%2d/%d", L.titulo, cur + 1, L.n);
        int top = L.n <= 3 ? 0 : (cur < 1 ? 0 : (cur > L.n - 2 ? L.n - 3 : cur - 1));
        for (int r = 0; r < 3 && top + r < L.n; r++) {
          const uint8_t it = L.it[top + r];
          valor(it, b, sizeof(b));
          const char m = (top + r) == cur ? '>' : ' ';
          if (b[0]) snprintf(l[r + 1], 21, "%c%-11.11s%8.8s", m, corto(it), b);
          else      snprintf(l[r + 1], 21, "%c%.19s", m, corto(it));          // sin valor: el nombre puede ser más largo
        }
      } else {
        snprintf(l[0], 21, ">%s", corto(L.it[cur]));
        valor(L.it[cur], l[1], 21);
      }
      break;
    }

    case M_WIFI:
      if (wifiConf) {
        if (filas >= 4) {
          snprintf(l[0], 21, "CONFIGURAR WI-FI");
          snprintf(l[1], 21, "Red actual:");
          snprintf(l[2], 21, "%.20s", wifiRedGuardada().c_str());
          snprintf(l[3], 21, confSi ? "Nueva red? NO [SI]" : "Nueva red?[NO] SI");
        } else {
          snprintf(l[0], 21, "Config nueva red?");
          snprintf(l[1], 21, "%s", siNo(filas));
        }
      } else if (filas >= 4) {
        snprintf(l[0], 21, "Conecte el celu a:");
        snprintf(l[1], 21, "%s", WIFI_AP_NOMBRE);
        snprintf(l[2], 21, "-> %s/wifi", WIFI_AP_IP);
        snprintf(l[3], 21, "Click=ok Mant=cerrar");
      } else {
        snprintf(l[0], 21, "Red %s", WIFI_AP_NOMBRE);
        snprintf(l[1], 21, "%s/wifi", WIFI_AP_IP);
      }
      break;

    case M_CAL: {
      const CalibEstado c = calibEstado();
      if (calPaso == 2) {
        if (filas >= 4) {
          snprintf(l[0], 21, "Frasco cerrado con");
          snprintf(l[1], 21, "sal comun + sensor");
          snprintf(l[2], 21, "Clima se apaga. Ok?");
          snprintf(l[3], 21, "%s", siNo(filas));
        } else { snprintf(l[0], 21, "Iniciar calib?"); snprintf(l[1], 21, "%s", siNo(filas)); }
      } else if (calPaso == 10) {
        if (filas >= 4) {
          snprintf(l[0], 21, "CALIBRANDO  %3uh%02um", (unsigned)(c.minutos / 60), (unsigned)(c.minutos % 60));
          if (isnan(c.actual)) snprintf(l[1], 21, "Mide --   Ref %.1f", c.ref);
          else                 snprintf(l[1], 21, "Mide %.1f Ref %.1f", c.actual, c.ref);
          if (isnan(c.prom)) snprintf(l[2], 21, "Prom: esperando...");
          else               snprintf(l[2], 21, "Prom %.2f Var %.2f", c.prom, c.rango);
          if (c.estable)       snprintf(l[3], 21, "ESTABLE  Click=opc.");
          else if (c.sePuede)  snprintf(l[3], 21, "Variando Click=opc.");
          else                 snprintf(l[3], 21, "Faltan %um Click=opc", (unsigned)(CAL_MIN_MIN - c.minutos));
        } else {
          snprintf(l[0], 21, "Cal %.1f/%.1f", isnan(c.actual) ? 0.0f : c.actual, c.ref);
          snprintf(l[1], 21, "%s %um", c.estable ? "Estable" : "Espere", (unsigned)c.minutos);
        }
      } else if (calPaso == 11) {
        static const char* const OP[3] = { "Aplicar ajuste", "Cancelar calib.", "Volver" };
        if (filas >= 4) {
          snprintf(l[0], 21, "CALIBRACION");
          for (int i = 0; i < 3; i++) snprintf(l[i + 1], 21, "%c%s", i == calOp ? '>' : ' ', OP[i]);
        } else { snprintf(l[0], 21, "Calibracion"); snprintf(l[1], 21, ">%s", OP[calOp]); }
      } else {                                        // 12 aplicar / 13 cancelar
        if (filas >= 4) {
          if (calPaso == 12) { snprintf(l[0], 21, "Guardar ajuste hum.?"); snprintf(l[1], 21, "%+.1f%% (antes %+.1f)", c.propuesto, cfg.offH); snprintf(l[2], 21, c.estable ? "Lectura estable" : "Ojo: aun varia"); }
          else               { snprintf(l[0], 21, "Cancelar la"); snprintf(l[1], 21, "calibracion?"); snprintf(l[2], 21, "El ajuste no cambia"); }
          snprintf(l[3], 21, "%s", siNo(filas));
        } else {
          snprintf(l[0], 21, calPaso == 12 ? "Guardar %+.1f%%?" : "Cancelar calib?", c.propuesto);
          snprintf(l[1], 21, "%s", siNo(filas));
        }
      }
      break;
    }

    case M_PARADA:
      if (filas >= 4) {
        if (paradaPaso == 1) {
          snprintf(l[0], 21, "APAGAR TODO!");
          snprintf(l[1], 21, "Apaga los 6 reles y");
          snprintf(l[2], 21, "el clima (sin ctrl)");
          snprintf(l[3], 21, "%s", siNo(filas));
        } else if (paradaPaso == 2) {
          snprintf(l[0], 21, "ULTIMA CONFIRMACION");
          snprintf(l[1], 21, "Sin proteccion de la");
          snprintf(l[2], 21, "temperatura. Seguro?");
          snprintf(l[3], 21, "%s", siNo(filas));
        } else {
          snprintf(l[0], 21, "REANUDAR TODO");
          snprintf(l[1], 21, "El clima vuelve a");
          snprintf(l[2], 21, "funcionar. Seguro?");
          snprintf(l[3], 21, "%s", siNo(filas));
        }
      } else {
        snprintf(l[0], 21, paradaPaso == 1 ? "Apagar TODO?" : paradaPaso == 2 ? "Seguro? Sin ctrl" : "Reanudar todo?");
        snprintf(l[1], 21, "%s", siNo(filas));
      }
      break;

    case M_TEST:
      if (testPaso == 3) { menuLineasTest(l, filas); break; }
      if (filas >= 4) {
        snprintf(l[0], 21, testPaso == 1 ? "TEST RELES! PELIGRO" : "ULTIMA CONFIRMACION");
        snprintf(l[1], 21, testPaso == 1 ? "Desenchufe los 220V" : "Heladera desenchuf.?");
        snprintf(l[2], 21, "%s", testPaso == 1 ? "de TODOS los equipos" :
                 testModoMenu == TEST_SUMA ? "Suma reles 1 a 6" : testModoMenu == TEST_TODOS ? "Prende los 6 juntos" : "Prende 1x1 los 6");
        snprintf(l[3], 21, "%s", siNo(filas));
      } else {
        snprintf(l[0], 21, testPaso == 1 ? "Desenchufe 220V!" : testModoMenu == TEST_UNO ? "Seguro? Test 1x1" : "Seguro? Varios ON");
        snprintf(l[1], 21, "%s", siNo(filas));
      }
      break;

    case M_INFO: {
      if (infoAcerca) {                      // Acerca de: autor y version del software
        if (filas >= 4) {
          snprintf(l[0], 21, "Camara de curado");
          snprintf(l[1], 21, "Software hecho por");
          snprintf(l[2], 21, "%s", SOFTWARE_AUTOR);
          snprintf(l[3], 21, "Version %s", SOFTWARE_VERSION);
        } else {
          snprintf(l[0], 21, "%s", SOFTWARE_AUTOR);
          snprintf(l[1], 21, "Version %s", SOFTWARE_VERSION);
        }
        break;
      }
      char ip[24], senal[24];
      datosRed(ip, sizeof(ip), senal, sizeof(senal));
      if (filas >= 4) {
        snprintf(l[0], 21, "RED %.16s", wifiRedGuardada().c_str());
        snprintf(l[1], 21, "IP %s", ip);
        snprintf(l[2], 21, "%s", senal);
        snprintf(l[3], 21, "Click para volver");
      } else {
        snprintf(l[0], 21, "IP");
        snprintf(l[1], 21, "%s", ip);
      }
      break;
    }

    case M_BAL:
      menuLineasBalanza(l, filas);
      break;

    case M_EDIT:
      textoValor(sel, valorActual(sel), act, sizeof(act));
      textoValor(sel, editVal, nue, sizeof(nue));
      if (filas >= 4) {
        snprintf(l[0], 21, "%s", largo(sel));
        snprintf(l[1], 21, "Actual: %s", act);
        snprintf(l[2], 21, "Nuevo:  %s", nue);
        snprintf(l[3], 21, "Gira=cambia Click=ok");
      } else {
        snprintf(l[0], 21, "%s", corto(sel));
        snprintf(l[1], 21, "Nuevo: %s", nue);
      }
      break;

    case M_CONF:
      textoValor(sel, valorActual(sel), act, sizeof(act));
      textoValor(sel, editVal, nue, sizeof(nue));
      if (filas >= 4) {
        snprintf(l[0], 21, "%s", largo(sel));
        if (sel >= T_FHUM && sel <= T_FFRIO && (int)(editVal + 0.5f) != MAN_AUTO)
          snprintf(l[1], 21, "%s -> %s (%umin)", act, nue, (unsigned)cfg.manualMin);
        else
          snprintf(l[1], 21, "%s -> %s", act, nue);
        snprintf(l[2], 21, "%s", esAccion(sel) ? "Aplicar?" : "Guardar el cambio?");
        snprintf(l[3], 21, "%s", siNo(filas));
      } else {
        snprintf(l[0], 21, "%s %s", esAccion(sel) ? "Aplicar?" : "Guardar?", nue);
        snprintf(l[1], 21, "%s", siNo(filas));
      }
      break;

    case M_MSG:
      if (filas >= 4) { snprintf(l[1], 21, "%s", msgTxt[0]); snprintf(l[2], 21, "%s", msgTxt[1]); }
      else            { snprintf(l[0], 21, "%s", msgTxt[0]); snprintf(l[1], 21, "%s", msgTxt[1]); }
      break;

    default: break;
  }
}

// Pantallas de la balanza:
//   BALANZA   conectada        CONEXION   ACTIVADA       Olvidar la balanza?
//   Merma  12.3% obj 30%       Estado: conectada         Se busca de nuevo
//   Peso   523.4 g  quieto     -60dBm Excelente          por su nombre
//   Dia 12   faltan ~14 d      IP 192.168.18.90              [NO]      SI
void menuLineasBalanza(char l[4][21], uint8_t filas) {
#if USAR_BALANZA
  const BalanzaEstado b = balanzaEstado();
  static const char* const EST[4] = { "apagada", "buscando", "sin respuesta", "conectada" };
  if (balPant == 2) {
    if (filas >= 4) {
      snprintf(l[0], 21, "Olvidar la balanza?");
      snprintf(l[1], 21, "Se busca de nuevo");
      snprintf(l[2], 21, "por su nombre");
      snprintf(l[3], 21, "%s", siNo(filas));
    } else { snprintf(l[0], 21, "Olvidar balanza?"); snprintf(l[1], 21, "%s", siNo(filas)); }
    return;
  }
  if (balPant == 1) {
    char senal[24];
    if (b.conectada && b.rssi) textoSenal(senal, sizeof(senal), b.rssi); else snprintf(senal, sizeof(senal), "Senal --");
    if (filas >= 4) {
      snprintf(l[0], 21, "CONEXION %11s", b.activo ? "ACTIVADA" : "APAGADA");
      snprintf(l[1], 21, "Estado: %s", EST[b.estadoCon & 3]);
      snprintf(l[2], 21, "%s", senal);
      if (b.vinculada[0]) snprintf(l[3], 21, "IP %s", b.vinculada); else snprintf(l[3], 21, "%.20s", BALANZA_HOST ".local ?");
    } else {
      snprintf(l[0], 21, "Balanza %s", b.activo ? "activada" : "apagada");
      snprintf(l[1], 21, "%s", EST[b.estadoCon & 3]);
    }
    return;
  }
  const PesajeEstado s = pesajeEstado();
  char merma[21], peso[21], pie[21];
  if (s.activo && !isnan(s.merma)) snprintf(merma, sizeof(merma), "Merma %5.1f%% obj%3.0f%%", s.merma, s.objetivo);
  else if (s.activo)               snprintf(merma, sizeof(merma), "Merma  --  obj%3.0f%%", s.objetivo);
  else                             snprintf(merma, sizeof(merma), "Sin seguimiento");
  if (b.hayPeso)                   snprintf(peso, sizeof(peso), "Peso %6.1f g %s", b.gramos, (b.flags & BAL_ESTABLE) ? "quieto" : "");
  else if (s.activo && !isnan(s.peso)) snprintf(peso, sizeof(peso), "Peso %6.1f g (ult)", s.peso);
  else                             snprintf(peso, sizeof(peso), "Peso   --");
  if (!s.activo)                   snprintf(pie, sizeof(pie), "Empezar en la web");
  else if (s.pausado)              snprintf(pie, sizeof(pie), "Dia %-3u EN PAUSA", (unsigned)s.dias);
  else if (s.descolgada)           snprintf(pie, sizeof(pie), "Pieza descolgada?");
  else if (!isnan(s.falta) && s.falta <= 0) snprintf(pie, sizeof(pie), "Dia %u OBJETIVO OK", (unsigned)s.dias);
  else if (!isnan(s.falta))        snprintf(pie, sizeof(pie), "Dia %-3u faltan ~%ud", (unsigned)s.dias, (unsigned)ceilf(s.falta));
  else                             snprintf(pie, sizeof(pie), "Dia %-3u estimando..", (unsigned)s.dias);
  if (filas >= 4) {
    snprintf(l[0], 21, "BALANZA %12s", EST[b.conectada ? 3 : (b.estadoCon & 3)]);
    snprintf(l[1], 21, "%s", merma);
    snprintf(l[2], 21, "%s", peso);
    snprintf(l[3], 21, "%s", pie);
  } else {
    if (s.activo && !isnan(s.merma)) snprintf(l[0], 21, "Merma %.1f/%.0f%%", s.merma, s.objetivo);
    else snprintf(l[0], 21, "%s", merma);
    if (!b.hayPeso)                          snprintf(l[1], 21, "%s", EST[b.estadoCon & 3]);
    else if (!s.activo || isnan(s.falta))    snprintf(l[1], 21, "%.1f g", b.gramos);
    else if (s.falta <= 0)                   snprintf(l[1], 21, "%.0fg LISTO", b.gramos);
    else                                     snprintf(l[1], 21, "%.0fg faltan~%ud", b.gramos, (unsigned)ceilf(s.falta));
  }
#else
  (void)filas;
  snprintf(l[0], 21, "Balanza desactivada");
#endif
}

// Pantalla del test de relés en curso (también la usa la pantalla normal si el test se inició desde la web).
// Muestra los 6 relés con su número, nombre y estado, en los tres modos:
//   TEST TODOS     118s
//   1 Refr ON  2 Humi ON
//   3 Desh ON  4 Vent ON
//   5 AUX1 ON  6 AUX2 ON
void menuLineasTest(char l[4][21], uint8_t filas) {
  for (int i = 0; i < 4; i++) l[i][0] = 0;
  static const char* const TIT[4] = { "TEST", "TEST 1 A 1", "TEST SUMAR", "TEST TODOS" };
  const uint8_t modo = controlTestModo() & 3, m = controlTestMascara();
  const unsigned seg = controlTestSegRestantes();
  auto prendido = [&](uint8_t k) { return (m >> controlTestOrden(k)) & 1; };   // relé físico k+1
  if (filas >= 4) {
    snprintf(l[0], 21, "%-15s%4us", TIT[modo], seg);
    for (uint8_t f = 0; f < 3; f++) {
      char a[12], b[12];
      snprintf(a, sizeof(a), "%u %-4.4s %s", (unsigned)(2 * f + 1), controlNombreRele(controlTestOrden(2 * f)),     prendido(2 * f)     ? "ON" : "--");
      snprintf(b, sizeof(b), "%u %-4.4s %s", (unsigned)(2 * f + 2), controlNombreRele(controlTestOrden(2 * f + 1)), prendido(2 * f + 1) ? "ON" : "--");
      snprintf(l[f + 1], 21, "%-9s  %-9s", a, b);
    }
  } else {
    snprintf(l[0], 21, "%-11s%4us", TIT[modo], seg);
    char d[7];
    for (uint8_t k = 0; k < 6; k++) d[k] = prendido(k) ? (char)('1' + k) : '-';
    d[6] = 0;
    snprintf(l[1], 21, "ON: %s", d);
  }
}
