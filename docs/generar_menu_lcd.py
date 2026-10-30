# Genera docs/menu_lcd.html y docs/Menu_LCD.pdf (manual del menú del LCD 20x4, sacado de menu.cpp).
#   python docs/generar_menu_lcd.py
# Si cambia el menú en menu.cpp, actualizar las listas de acá y volver a correrlo.
import html, os, subprocess

AQUI = os.path.dirname(os.path.abspath(__file__))
CHROME = r"C:\Program Files\Google\Chrome\Application\chrome.exe"


def lcd(*filas, chico=False):
    """Pantalla de 20x4 (o 16x2) con el texto tal como lo arma el programa."""
    ancho = 16 if chico else 20
    n = 2 if chico else 4
    f = list(filas) + [""] * (n - len(filas))
    txt = "\n".join(html.escape(x[:ancho].ljust(ancho)) for x in f[:n])
    return f'<div class="lcd{" chico" if chico else ""}"><pre>{txt}</pre></div>'


def fila_lista(sel, nombre, valor=""):
    m = ">" if sel else " "
    if valor:
        return f"{m}{nombre[:11]:<11}{valor[:8]:>8}"
    return f"{m}{nombre[:19]}"


def lista(titulo, items, cursor=0):
    """items: [(nombre, valor)]. Muestra 3 renglones alrededor del cursor, como el programa."""
    n = len(items)
    top = 0 if n <= 3 else (0 if cursor < 1 else (n - 3 if cursor > n - 2 else cursor - 1))
    filas = [f"{titulo:<15}{cursor + 1:>2}/{n}"]
    for r in range(3):
        if top + r < n:
            nom, val = items[top + r]
            filas.append(fila_lista(top + r == cursor, nom, val))
    return lcd(*filas)


def tabla(filas):
    h = '<table><tr><th>Opción (LCD)</th><th>Qué hace</th><th>Valores</th></tr>'
    for a, b, c in filas:
        h += f"<tr><td class='op'>{a}</td><td>{b}</td><td class='val'>{c}</td></tr>"
    return h + "</table>"


S = []  # secciones
add = S.append

# ---------------------------------------------------------------- portada / uso
add(f"""
<h1>Cámara de curado — Menú del LCD</h1>
<p class="sub">Pantalla 20x4 + encoder rotativo con pulsador · firmware v1.0.8 OTA · generado desde <code>menu.cpp</code></p>

<h2>Cómo se maneja</h2>
<table class="ctl">
<tr><th>Acción</th><th>En la pantalla normal</th><th>Dentro del menú</th></tr>
<tr><td><b>Click</b></td><td>Entra al menú</td><td>Elige / acepta</td></tr>
<tr><td><b>Girar</b></td><td>—</td><td>Recorre las opciones o cambia el valor (girando rápido avanza de a 5 pasos)</td></tr>
<tr><td><b>Mantener apretado</b></td><td>Atajo: <b>pausar / reanudar el control</b> (pide confirmación)</td><td>Sale sin guardar</td></tr>
<tr><td><b>20 s sin tocar</b></td><td>—</td><td>Sale sin guardar (en las pantallas de la Balanza: 5 min)</td></tr>
<tr><td><b>Encoder apretado al encender</b> (1,5 s)</td><td colspan="2">Abre el modo de configuración de la red Wi-Fi</td></tr>
</table>
<ul class="notas">
<li><b>Todo cambio pide confirmación SI/NO</b>, y el cursor arranca siempre en <b>NO</b>: hay que girar a SI y hacer click.</li>
<li>Al confirmar aparece <b>"Guardado"</b> (ajustes, quedan en memoria) o <b>"Listo"</b> (acciones inmediatas: AUX, forzados, balanza).</li>
<li>Las opciones con <b>&gt;</b> abren un submenú. <b>Volver</b> sube un nivel y recuerda dónde estabas.</li>
<li>Durante un test de relés iniciado desde la web, un click en la pantalla normal lo detiene.</li>
</ul>

<h2>Pantalla normal (fuera del menú)</h2>
<div class="pantallas">
{lcd("T  14.3C  obj 14.0C", "H  76.4% obj 75.0%", "Fri:-- Hum:ON Des:--", "Ven:ON AUX1:-- AUX2:")}
<div class="expl">
<p>Renglón 1 y 2: temperatura y humedad medidas, con su objetivo.</p>
<p>Renglón 3: relés de clima (refrigeración, humidificador, deshumidificador).</p>
<p>Renglón 4: <b>rota cada 4 s</b> entre ventilador/AUX1/AUX2, la cuenta regresiva de los forzados manuales
(<code>MANUAL Hum ON 12:30</code>) y los avisos: <code>CONTROL EN PAUSA</code>, <code>ALARMA TEMPERATURA</code>,
<code>CALIBRANDO SENSOR</code>, <code>Sin Wi-Fi</code>, <code>CONFIG WI-FI (celu)</code>.</p>
</div></div>
""")

# ---------------------------------------------------------------- árbol
add("""
<h2>Mapa del menú</h2>
<pre class="arbol">
MENU
├── Temperatura  &gt;      Objetivo · Histeresis · Alarma +/- · Min encend. · Forzar frio
├── Humedad      &gt;      Objetivo · Histeresis · Min encend. · Forzar hum. · Forzar desh
├── Ventilador   &gt;      Programa · Cada · Duracion
├── AUX1                 ON / OFF          (el nombre se cambia en la web)
├── AUX2                 ON / OFF
├── Control              Activo / PAUSA
├── Balanza      &gt;      Peso/merma · Seguimiento · Conexion · Estado red · Buscar · Olvidar IP
├── Tiempos      &gt;      Min apagado · Dur forzado
├── Calibracion  &gt;      Ajuste T · Ajuste H · Calibr. sal
├── Diagnostico  &gt;      Red e IP · Config WiFi · Test reles · Acerca de
├── APAGAR TODO         parada de emergencia (pasa a REANUDAR TODO mientras está activa)
└── Salir
</pre>
<p class="nota">Cada submenú termina en <b>Volver</b>. Con <code>USAR_BALANZA 0</code> no aparece Balanza; con
<code>TEST_AVANZADO 1</code>, "Test reles" abre un submenú con tres modos (De a uno · Sumar 1 a 6 · Todos juntos).</p>
""")

# ---------------------------------------------------------------- menú principal
PRINC = [("Temperatura", ">"), ("Humedad", ">"), ("Ventilador", ">"), ("AUX1", "OFF"), ("AUX2", "OFF"),
         ("Control", "Activo"), ("Balanza", ">"), ("Tiempos", ">"), ("Calibracion", ">"), ("Diagnostico", ">"), ("APAGAR TODO", ""), ("Salir", "")]
add(f"""
<h2 class="nueva">MENU (principal)</h2>
<div class="pantallas">{lista("MENU", PRINC, 0)}{lista("MENU", PRINC, 5)}{lista("MENU", PRINC, 11)}</div>
{tabla([
 ("Temperatura &gt;", "Submenú de temperatura y refrigeración.", "—"),
 ("Humedad &gt;", "Submenú de humedad, humidificador y deshumidificador.", "—"),
 ("Ventilador &gt;", "Programa del ventilador.", "—"),
 ("AUX1 / AUX2", "Prende o apaga a mano los relés 5 y 6. Muestran el nombre elegido en la web (hasta 12 letras).", "ON · OFF"),
 ("Control", "Pausa el control automático: clima apagado (salvo el frío de seguridad si hace demasiado calor). AUX1/AUX2 no se tocan. También con el atajo: mantener apretado en la pantalla normal.", "Activo · PAUSA"),
 ("Balanza &gt;", "Peso y merma de la pieza testigo, conexión con la balanza Wi-Fi.", "—"),
 ("Tiempos &gt;", "Tiempos que valen para los tres relés de clima.", "—"),
 ("Calibracion &gt;", "Ajustes del sensor y asistente de calibración con sal.", "—"),
 ("Diagnostico &gt;", "Red, Wi-Fi, test de relés y versión.", "—"),
 ("APAGAR TODO", "<b>Parada de emergencia.</b> Apaga los 6 relés y suspende el control (sin clima, sin ventilador y sin protección de temperatura). Pide dos confirmaciones, con el cursor siempre en NO. Se mantiene aunque se reinicie el equipo. Mientras está activa el ítem se llama <b>REANUDAR TODO</b> y la pantalla normal muestra <b>** PARADA TOTAL **</b> fijo.", "—"),
 ("Salir", "Vuelve a la pantalla normal.", "—"),
])}
<h3>Parada de emergencia</h3>
<div class="pantallas">
{lcd("APAGAR TODO!", "Apaga los 6 reles y", "el clima (sin ctrl)", "    [NO]      SI")}
{lcd("ULTIMA CONFIRMACION", "Sin proteccion de la", "temperatura. Seguro?", "    [NO]      SI")}
{lcd("REANUDAR TODO", "El clima vuelve a", "funcionar. Seguro?", "    [NO]      SI")}
</div>
<p class="nota">En la web es el botón rojo <b>APAGAR TODO</b> de arriba. Un botón por software no reemplaza cortar la corriente.</p>
""")

# ---------------------------------------------------------------- temperatura
TEMP = [("Objetivo", "14.0C"), ("Histeresis", "2.0C"), ("Alarma +/-", "3.0C"), ("Min encend.", "120s"), ("Forzar frio", "Auto"), ("Volver", "")]
add(f"""
<h2 class="nueva">TEMPERATURA</h2>
<div class="pantallas">{lista("TEMPERATURA", TEMP, 0)}{lista("TEMPERATURA", TEMP, 4)}</div>
{tabla([
 ("Objetivo", "Temperatura objetivo. La heladera prende si T &gt; objetivo + histéresis y apaga al llegar al objetivo.", "5,0 a 30,0 °C, de a 0,5"),
 ("Histeresis", "Cuánto se deja subir la temperatura antes de prender el frío.", "0,5 a 5,0 °C, de a 0,5"),
 ("Alarma +/-", "Si la temperatura se aleja más que esto del objetivo, aparece ALARMA TEMPERATURA.", "1,0 a 10,0 °C, de a 0,5"),
 ("Min encend.", "Tiempo mínimo encendida la refrigeración (protege el compresor).", "0 a 600 s, de a 10"),
 ("Forzar frio", f"Fuerza la refrigeración a mano durante \"Dur forzado\" minutos y después vuelve sola a Auto. Encender respeta el mínimo apagado: si no se puede todavía, avisa <code>No se puede aun / Espera m:ss</code>.", "Auto · ON · OFF"),
])}
""")

# ---------------------------------------------------------------- humedad
HUM = [("Objetivo", "75.0%"), ("Histeresis", "3.0%"), ("Min encend.", "60s"), ("Forzar hum.", "Auto"), ("Forzar desh", "Auto"), ("Volver", "")]
add(f"""
<h2 class="nueva">HUMEDAD</h2>
<div class="pantallas">{lista("HUMEDAD", HUM, 0)}{lista("HUMEDAD", HUM, 3)}</div>
{tabla([
 ("Objetivo", "Humedad objetivo. Humidificador: prende si HR &lt; objetivo − histéresis. Deshumidificador: prende si HR &gt; objetivo + histéresis. Los dos apagan al llegar al objetivo.", "40,0 a 100,0 %, de a 0,5"),
 ("Histeresis", "Margen alrededor del objetivo.", "0,5 a 10,0 %, de a 0,5"),
 ("Min encend.", "Tiempo mínimo encendidos el humidificador y el deshumidificador.", "0 a 600 s, de a 10"),
 ("Forzar hum.", "Fuerza el humidificador a mano. Prender uno apaga el otro (nunca los dos juntos).", "Auto · ON · OFF"),
 ("Forzar desh", "Fuerza el deshumidificador a mano.", "Auto · ON · OFF"),
])}
""")

# ---------------------------------------------------------------- ventilador
VENT = [("Programa", "Activo"), ("Cada", "4 h"), ("Duracion", "15min"), ("Volver", "")]
add(f"""
<h2 class="nueva">VENTILADOR</h2>
<div class="pantallas">{lista("VENTILADOR", VENT, 0)}</div>
{tabla([
 ("Programa", "Habilita el programa del ventilador.", "Apagado · Activo"),
 ("Cada", "Cada cuántas horas arranca un ciclo (siempre a la misma hora del día; la hora de arranque se elige en la web).", "1, 2, 3, 4, 6, 8, 12, 24 h"),
 ("Duracion", "Minutos encendido en cada ciclo (si es mayor o igual al período, queda encendido siempre).", "0 a 180 min, de a 1"),
])}
""")

# ---------------------------------------------------------------- balanza
BAL = [("Peso/merma", ""), ("Seguimiento", "Sigue"), ("Conexion", "ON"), ("Estado red", ""), ("Buscar", ""), ("Olvidar IP", ""), ("Volver", "")]
add(f"""
<h2 class="nueva">BALANZA</h2>
<div class="pantallas">{lista("BALANZA", BAL, 0)}{lista("BALANZA", BAL, 4)}</div>
{tabla([
 ("Peso/merma", "Abre la pantalla del peso. <b>Girar</b> alterna con la de conexión; click vuelve.", "pantalla"),
 ("Seguimiento", "Pausa o retoma el seguimiento de merma (para descolgar la pieza testigo y limpiar o revisar). Si no hay seguimiento, avisa <code>Sin seguimiento / Empezar en la web</code>.", "Sigue · PAUSA"),
 ("Conexion", "Activa o apaga la consulta a la balanza por Wi-Fi.", "ON · OFF"),
 ("Estado red", "Pantalla con el estado de la conexión, la señal y la IP de la balanza.", "pantalla"),
 ("Buscar", "Busca de nuevo la balanza por su nombre (balanza.local). Muestra <code>Buscando la balanza en la red</code>.", "acción"),
 ("Olvidar IP", "Borra la IP guardada y vuelve a buscar por nombre (pide SI/NO).", "acción"),
])}
<div class="pantallas">
{lcd("BALANZA    conectada", "Merma  12.3% obj 30%", "Peso  523.4 g quieto", "Dia 12  faltan ~14d")}
{lcd("CONEXION    ACTIVADA", "Estado: conectada", "-60dBm Excelente", "IP 192.168.18.90")}
{lcd("Olvidar la balanza?", "Se busca de nuevo", "por su nombre", "    [NO]      SI")}
</div>
<p class="nota">El último renglón de la pantalla del peso puede decir: <code>Dia N faltan ~Xd</code>, <code>Dia N estimando..</code>,
<code>Dia N OBJETIVO OK</code>, <code>Dia N EN PAUSA</code>, <code>Pieza descolgada?</code> o <code>Empezar en la web</code>.
Tarar, calibrar e iniciar el seguimiento se hacen solo desde la web.</p>
""")

# ---------------------------------------------------------------- tiempos
TIEM = [("Min apagado", "180s"), ("Dur forzado", "30min"), ("Volver", "")]
add(f"""
<h2 class="nueva">TIEMPOS</h2>
<div class="pantallas">{lista("TIEMPOS", TIEM, 0)}</div>
{tabla([
 ("Min apagado", "Tiempo mínimo apagados los tres relés de clima antes de volver a prender. La heladera nunca menos de 180 s, aunque se ponga menos.", "0 a 600 s, de a 10"),
 ("Dur forzado", "Cuánto dura un forzado manual (Forzar frio / hum. / desh) antes de volver solo a Auto.", "5 a 120 min, de a 5"),
])}
""")

# ---------------------------------------------------------------- calibración
CAL = [("Ajuste T", "+0.0C"), ("Ajuste H", "-2.5%"), ("Calibr. sal", ""), ("Volver", "")]
add(f"""
<h2 class="nueva">CALIBRACION</h2>
<div class="pantallas">{lista("CALIBRACION", CAL, 0)}</div>
{tabla([
 ("Ajuste T", "Corrección que se suma a la temperatura medida.", "−5,0 a +5,0 °C, de a 0,1"),
 ("Ajuste H", "Corrección que se suma a la humedad medida.", "−15,0 a +15,0 %, de a 0,1"),
 ("Calibr. sal", "Asistente de calibración con sal común (75,3 % HR). Dice <code>ACTIVA</code> mientras calibra.", "asistente"),
])}
<h3>Asistente de calibración con sal</h3>
<div class="pantallas">
{lcd("Frasco cerrado con", "sal comun + sensor", "Clima se apaga. Ok?", "    [NO]      SI")}
{lcd("CALIBRANDO    1h12m", "Mide 77.1 Ref 75.3", "Prom 77.05 Var 0.20", "ESTABLE  Click=opc.")}
{lcd("CALIBRACION", ">Aplicar ajuste", " Cancelar calib.", " Volver")}
{lcd("Guardar ajuste hum.?", "-1.8% (antes +0.0)", "Lectura estable", "    [NO]      SI")}
</div>
<ol class="pasos">
<li>Se mete el sensor en un frasco cerrado con sal común húmeda y se confirma: <b>el clima se apaga</b> mientras dura.</li>
<li>La pantalla muestra lo medido, la referencia, el promedio y la variación. Hasta los 60 minutos dice <code>Faltan Nm</code>.</li>
<li>Click (o girar) abre las opciones: <b>Aplicar ajuste</b> (solo después de 60 min), <b>Cancelar calib.</b> o <b>Volver</b>
(la calibración sigue). Mantener apretado vuelve sin cancelar.</li>
<li>A las 24 h se cancela sola y el clima vuelve a funcionar.</li>
</ol>
""")

# ---------------------------------------------------------------- diagnóstico
DIAG = [("Red e IP", ""), ("Config WiFi", ""), ("Test reles", ""), ("Acerca de", ""), ("Volver", "")]
add(f"""
<h2 class="nueva">DIAGNOSTICO</h2>
<div class="pantallas">{lista("DIAGNOSTICO", DIAG, 0)}</div>
{tabla([
 ("Red e IP", "Red Wi-Fi, IP del equipo y señal (promediada, en pasos de 5 dBm).", "pantalla"),
 ("Config WiFi", "Crea la red propia <b>CamaraCurado-Setup</b> (sin clave) para elegir la red desde el celular en 192.168.4.1/wifi. Pide SI/NO.", "asistente"),
 ("Test reles", "Prende los 6 relés de a uno (3 s cada uno, 1 s de pausa) para comprobarlos. Pide dos confirmaciones. <b>¡Con los 220 V desenchufados!</b>", "asistente"),
 ("Acerca de", "Autor y versión del software.", "pantalla"),
])}
<div class="pantallas">
{lcd("RED MI-RED-WIFI", "IP 192.168.18.88", "-55dBm Excelente", "Click para volver")}
{lcd("CONFIGURAR WI-FI", "Red actual:", "MI-RED-WIFI", "Nueva red?[NO] SI")}
{lcd("Conecte el celu a:", "CamaraCurado-Setup", "-> 192.168.4.1/wifi", "Click=ok Mant=cerrar")}
</div>
<p class="nota">En las instrucciones de Config WiFi: click vuelve (la red de configuración sigue abierta) y mantener apretado la cierra
(<code>Config WiFi cerrada</code>).</p>
<h3>Test de relés</h3>
<div class="pantallas">
{lcd("TEST RELES! PELIGRO", "Desenchufe los 220V", "de TODOS los equipos", "    [NO]      SI")}
{lcd("ULTIMA CONFIRMACION", "Heladera desenchuf.?", "Prende 1x1 los 6", "    [NO]      SI")}
{lcd("TEST 1 A 1       18s", "1 Refr ON  2 Humi --", "3 Desh --  4 Vent --", "5 AUX1 --  6 AUX2 --")}
</div>
<ul class="notas">
<li>Mientras dura, el control automático, el ventilador y los forzados quedan suspendidos. Un click (o mantener) lo detiene.</li>
<li>Si la heladera estuvo apagada menos de 180 s no arranca: avisa <code>Heladera: proteccion / esperar m:ss</code>.</li>
<li>Al terminar: <code>Test terminado / Todo en automatico</code>; AUX1 y AUX2 vuelven a como estaban.</li>
</ul>
<div class="pantallas">{lcd("Camara de curado", "Software hecho por", "ForzaDos", "Version v1.0.8 OTA")}</div>
""")

# ---------------------------------------------------------------- editar / confirmar
add(f"""
<h2 class="nueva">Cambiar un valor (todas las opciones)</h2>
<div class="pantallas">
{lcd("Temperatura objetivo", "Actual: 14.0C", "Nuevo:  15.5C", "Gira=cambia Click=ok")}
{lcd("Temperatura objetivo", "14.0C -> 15.5C", "Guardar el cambio?", "     NO      [SI]")}
{lcd("", "Guardado", "", "")}
</div>
<ol class="pasos">
<li>Click en la opción: aparece el valor actual y el nuevo. Girar cambia el nuevo (rápido = de a 5 pasos).</li>
<li>Click: si no cambió nada, vuelve a la lista; si cambió, pide confirmación (<b>arranca en NO</b>).</li>
<li>Girar a SI y click: <code>Guardado</code> (o <code>Listo</code> en AUX, forzados y balanza). Mantener apretado en cualquier paso cancela.</li>
<li>En los forzados, la confirmación muestra la duración: <code>Auto -&gt; ON (30min)</code>.</li>
</ol>
<h3>Atajo de pausa (pantalla normal, mantener apretado)</h3>
<div class="pantallas">{lcd("Control automatico", "Activo -> PAUSA", "Guardar el cambio?", "    [NO]      SI")}</div>
""")

CSS = """
@page { size: A4; margin: 14mm 14mm 16mm; }
body { font-family: 'Segoe UI', Arial, sans-serif; font-size: 10.5pt; color: #1d1d1d; line-height: 1.38; }
h1 { font-size: 22pt; margin: 0 0 2px; color: #7a2e17; }
.sub { color: #666; margin: 0 0 14px; }
h2 { font-size: 14pt; color: #7a2e17; border-bottom: 2px solid #e0623f; padding-bottom: 3px; margin: 18px 0 8px; }
h2.nueva { break-before: auto; }
section { break-inside: avoid; }
h2, h3 { break-after: avoid; }
h3 { font-size: 11.5pt; margin: 14px 0 6px; color: #333; }
table { border-collapse: collapse; width: 100%; margin: 8px 0; font-size: 9.6pt; break-inside: auto; }
tr { break-inside: avoid; }
th { background: #f3e6df; text-align: left; padding: 5px 6px; border: 1px solid #d9c4b8; }
td { padding: 5px 6px; border: 1px solid #e2d6cf; vertical-align: top; }
td.op { font-family: Consolas, monospace; white-space: nowrap; font-weight: 600; }
td.val { white-space: nowrap; color: #444; }
.ctl td:first-child { white-space: nowrap; }
.pantallas { display: flex; flex-wrap: wrap; gap: 10px; margin: 8px 0; align-items: flex-start; break-inside: avoid; }
.lcd { background: #1e3d8f; border: 5px solid #222; border-radius: 6px; padding: 6px 8px; box-shadow: inset 0 0 6px #0008; }
.lcd pre { margin: 0; font-family: Consolas, 'Courier New', monospace; font-size: 10.5pt; line-height: 1.25; color: #e8f0ff; letter-spacing: .5px; }
.expl { flex: 1; min-width: 220px; font-size: 9.8pt; } .expl p { margin: 0 0 5px; }
pre.arbol { background: #faf6f3; border: 1px solid #e2d6cf; padding: 10px 12px; font-family: Consolas, monospace; font-size: 9.6pt; line-height: 1.5; }
.nota { font-size: 9.4pt; color: #555; }
ul.notas, ol.pasos { font-size: 9.8pt; padding-left: 20px; margin: 6px 0; } li { margin: 2px 0; }
code { font-family: Consolas, monospace; background: #f3eeea; padding: 0 3px; border-radius: 3px; font-size: 9.2pt; }
"""

doc = f"<!doctype html><html lang='es'><head><meta charset='utf-8'><title>Menú del LCD — Cámara de curado</title><style>{CSS}</style></head><body>{''.join(f'<section>{x}</section>' for x in S)}</body></html>"
ruta_html = os.path.join(AQUI, "menu_lcd.html")
ruta_pdf = os.path.join(AQUI, "Menu_LCD.pdf")
open(ruta_html, "w", encoding="utf-8").write(doc)
subprocess.run([CHROME, "--headless=new", "--disable-gpu", "--no-pdf-header-footer",
                f"--print-to-pdf={ruta_pdf}", "file:///" + ruta_html.replace("\\", "/")], check=True,
               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
print("PDF:", ruta_pdf)
