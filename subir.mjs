// Compila la cámara de curado y la sube por Wi-Fi (OTA). Desde PROYECTOS_CLAUDE/camara_curado:
//   node subir.mjs [ip | COMx]       (sin nada: 192.168.18.88)
// - Usa partitions.csv (dos lugares de 1,44 MB para el programa). La primera vez con esta partición,
//   y si algún día el OTA no anda, se sube por USB:  node subir.mjs COM3
// - La web no pide clave (WEB_SIN_CLAVE 1 en config.h). Si la activás (0), la clave sale de CAMARA_CLAVE o de WEB_CLAVE.
import { execFileSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { createHash, randomBytes } from 'node:crypto';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const AQUI = dirname(fileURLToPath(import.meta.url));
const CLI = 'C:/Program Files/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe';
const FQBN = 'esp32:esp32:esp32:PartitionScheme=custom';
const MAXIMO = 0x170000; // tamaño de cada lugar para el programa (partitions.csv)
const BUILD = join(AQUI, 'build', 'ota');
const destino = process.argv[2] || '192.168.18.88';

const cfg = readFileSync(join(AQUI, 'config.h'), 'utf8');
const definido = (n) => cfg.match(new RegExp(`#define\\s+${n}\\s+"([^"]*)"`))?.[1];
const usuario = definido('WEB_USUARIO') || 'admin';
const clave = process.env.CAMARA_CLAVE || definido('WEB_CLAVE');
const correr = (args) => execFileSync(CLI, args, { cwd: AQUI, stdio: 'inherit' });

console.log(`== Compilando camara_curado ${definido('VERSION_PROGRAMA')}`);
correr(['compile', '-b', FQBN, '--build-property', `upload.maximum_size=${MAXIMO}`, '--build-path', BUILD, AQUI]);

if (destino.toUpperCase().startsWith('COM')) {
  console.log(`== Subiendo por USB a ${destino}`);
  correr(['upload', '-p', destino, '-b', FQBN, '--input-dir', BUILD, AQUI]);
  process.exit(0);
}

// HTTP Digest (la web de la cámara pide clave así).
const md5 = (s) => createHash('md5').update(s).digest('hex');
async function autorizacion(metodo, uri) {
  const r = await fetch(`http://${destino}/api/diag`, { signal: AbortSignal.timeout(8000) });
  const h = r.headers.get('www-authenticate') || '';
  if (r.status === 200) return '';   // la web no tiene clave
  if (r.status !== 401 || !/^Digest/i.test(h)) throw new Error(`respuesta inesperada de ${destino} (${r.status})`);
  const p = Object.fromEntries([...h.matchAll(/(\w+)="?([^",]*)"?/g)].map((m) => [m[1], m[2]]));
  const nc = '00000001', cnonce = randomBytes(8).toString('hex');
  const ha1 = md5(`${usuario}:${p.realm}:${clave}`), ha2 = md5(`${metodo}:${uri}`);
  const resp = md5(`${ha1}:${p.nonce}:${nc}:${cnonce}:auth:${ha2}`);
  return `Digest username="${usuario}", realm="${p.realm}", nonce="${p.nonce}", uri="${uri}", ` +
         `algorithm=MD5, response="${resp}", opaque="${p.opaque || ''}", qop=auth, nc=${nc}, cnonce="${cnonce}"`;
}

// Encabezado de clave, o nada si la web no la pide.
const cabeceraClave = async (metodo, uri) => { const a = await autorizacion(metodo, uri); return a ? { Authorization: a } : {}; };

const bin = readFileSync(join(BUILD, 'camara_curado.ino.bin'));
console.log(`== Subiendo por Wi-Fi a ${destino} (${Math.round(bin.length / 1024)} KB)`);
const fd = new FormData();
fd.append('bin', new Blob([bin]), 'camara_curado.ino.bin');
const r = await fetch(`http://${destino}/api/ota`, {
  method: 'POST',
  headers: { 'X-Camara': '1', ...(await cabeceraClave('POST', '/api/ota')) },
  body: fd,
  signal: AbortSignal.timeout(180000),
});
const texto = await r.text();
if (r.status === 401) { console.error('!! Clave incorrecta (si la cambiaste desde la web, pasala en CAMARA_CLAVE).'); process.exit(1); }
if (!r.ok) { console.error(`!! No se pudo actualizar (${r.status}): ${texto}`); process.exit(1); }
console.log('== Grabado. El equipo se reinicia; esperando que vuelva...');

const ini = Date.now();
for (;;) {
  await new Promise((ok) => setTimeout(ok, 4000));
  try {
    const d = await fetch(`http://${destino}/api/diag`, {
      headers: await cabeceraClave('GET', '/api/diag'), signal: AbortSignal.timeout(5000),
    }).then((x) => x.json());
    if (d.uptime < 300) { console.log(`== Listo: ${d.version} en ${d.particion} (se confirma solo al minuto, con Wi-Fi).`); break; }
  } catch { /* todavía reiniciando */ }
  if (Date.now() - ini > 120000) { console.error('!! No volvió a contestar en 2 minutos: revisalo.'); process.exit(1); }
}
