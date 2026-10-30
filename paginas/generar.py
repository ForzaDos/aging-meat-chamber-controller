"""
Genera pagina.h, pagina_bitacora.h y pagina_wifi.h (en la carpeta del sketch) a partir de los
.html de esta carpeta, comprimidos con gzip. La placa los envía tal cual con
"Content-Encoding: gzip" (como /chart.js): pesan ~70 % menos y la página carga más rápido.

Uso: editar el .html que corresponda y, ANTES de compilar en el IDE, ejecutar
    python generar.py
(con doble click también funciona). Además pone en la página la VERSION_PROGRAMA de config.h,
así la versión de la página y la del programa no pueden quedar distintas.
"""
import gzip
import os
import re
import sys

AQUI = os.path.dirname(os.path.abspath(__file__))
SKETCH = os.path.dirname(AQUI)

PAGINAS = [
    # html, .h, nombre del arreglo, comentario
    ('principal.html', 'pagina.h', 'PAGINA_HTML_GZ',
     'Página principal: monitoreo del secadero (HTML + CSS + JS). Chart.js se sirve desde el propio\n'
     '// equipo (/chart.js): todo funciona sin internet. El diario es otra página (/bitacora), con pestañas.\n'
     '// Los min/max de los deslizables tienen que coincidir con los rangos de hardware.h y web.cpp.'),
    ('bitacora.html', 'pagina_bitacora.h', 'PAGINA_BITACORA_HTML_GZ',
     'Página del diario de secado (HTML + CSS + JS). Se sirve en /bitacora.\n'
     '// Chart.js se sirve desde el propio equipo (/chart.js): todo funciona sin internet.'),
    ('wifi.html', 'pagina_wifi.h', 'PAGINA_WIFI_HTML_GZ',
     'Página para elegir la red Wi-Fi (se sirve en /wifi).'),
]

RE_VERSION_PAGINA = re.compile(rb"(const PAGINA_VERSION = ')[^']*(')")


def version_programa():
    cfg = open(os.path.join(SKETCH, 'config.h'), encoding='utf-8').read()
    m = re.search(r'#define\s+VERSION_PROGRAMA\s+"([^"]+)"', cfg)
    if not m:
        sys.exit('No encontré VERSION_PROGRAMA en config.h')
    return m.group(1)


def usar_balanza():
    hw = open(os.path.join(SKETCH, 'hardware.h'), encoding='utf-8').read()
    m = re.search(r'#define\s+USAR_BALANZA\s+(\d)', hw)
    return not m or m.group(1) != '0'


def main():
    version = version_programa().encode()
    con_balanza = usar_balanza()
    for html, h, nombre, comentario in PAGINAS:
        ruta_html = os.path.join(AQUI, html)
        datos = open(ruta_html, 'rb').read().replace(b'\r\n', b'\n')

        if html == 'principal.html':
            datos, n = RE_VERSION_PAGINA.subn(lambda m: m.group(1) + version + m.group(2), datos)
            if n != 1:
                sys.exit("principal.html: no encontré la línea const PAGINA_VERSION = '...'")
            open(ruta_html, 'wb').write(datos)          # el .html también muestra la versión vigente
            if not con_balanza:                         # sin balanza: la pestaña arranca oculta (no aparece y desaparece)
                datos = datos.replace(b'<body>', b'<body class="sinBal">', 1)

        gz = gzip.compress(datos, compresslevel=9, mtime=0)   # mtime=0: mismo .html -> mismo .h
        assert gzip.decompress(gz) == datos

        filas = [','.join(f'0x{b:02x}' for b in gz[i:i + 20]) for i in range(0, len(gz), 20)]
        with open(os.path.join(SKETCH, h), 'w', encoding='utf-8', newline='\n') as f:
            f.write('#pragma once\n#include <Arduino.h>\n\n')
            f.write(f'// {comentario}\n')
            f.write(f'// GENERADO por paginas/generar.py a partir de paginas/{html}: no editar a mano.\n')
            f.write(f'// {len(datos)} bytes de HTML -> {len(gz)} bytes con gzip.\n')
            f.write(f'const uint8_t {nombre}[] PROGMEM = {{\n')
            f.write(',\n'.join(filas))
            f.write('\n};\n')
        print(f'{html:15} {len(datos):7} -> {len(gz):6} bytes  ({h})')
    print(f'Versión de la página: {version.decode()}')


if __name__ == '__main__':
    main()
