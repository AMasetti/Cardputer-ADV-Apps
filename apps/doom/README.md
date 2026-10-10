# Doom para Cardputer / Cardputer-Adv

Doom corriendo en la M5Stack Cardputer, sin PSRAM, leyendo el WAD desde la
microSD. Esta carpeta **no copia el código del juego**: `build.sh` clona el
port de romalik en un commit fijo, le aplica los parches de `patches/` y
compila con ESP-IDF v4.4.6.

## Créditos y atribución

Todo el trabajo de portar Doom a la Cardputer es de otros. Esta app solo
agrega soporte para la Cardputer-Adv.

| Parte | Autor | Licencia |
|---|---|---|
| Port a Cardputer: [romalik/m5cardputer_doom](https://github.com/romalik/m5cardputer_doom) (mmap del WAD desde la SD, UI, wad_converter) | romalik | GPL-2.0-or-later (por el motor) |
| Motor: [GBADoom](https://github.com/doomhack/GBADoom), port de PrBoom a GBA, y GbaWadUtil | doomhack | GPL-2.0-or-later |
| [PrBoom](https://prboom.sourceforge.net/), basado en BOOM / LxDoom / LSDLDoom | id Software, Lee Killough, Jess Haas, Colin Phipps, Florian Schulze y equipo | GPL-2.0-or-later |
| Código fuente original de Doom | id Software | GPL-2.0 (liberado en 1999) |
| Partes de [Retro-Go](https://github.com/ducalex/retro-go) | ducalex | GPL-2.0 |
| HAL de la [M5Cardputer-UserDemo](https://github.com/m5stack/M5Cardputer-UserDemo) | M5Stack / Forairaaaaa | MIT |
| [LovyanGFX](https://github.com/lovyan03/LovyanGFX) | lovyan03 | FreeBSD |
| Secuencias de init del TCA8418 y del ES8311 (en el parche), de [M5Cardputer](https://github.com/m5stack/M5Cardputer) y [M5Unified](https://github.com/m5stack/M5Unified) | M5Stack | MIT |

## Licencia

**GPL-2.0-or-later** (ver [`LICENSE`](LICENSE)). El `LICENSE` del repo de
romalik dice MIT, pero es el que trajo la demo de M5Stack de la que partió;
el motor de Doom (PrBoom/GBADoom) es GPL-2.0-or-later, como figura en cada
archivo de `main/doom/`. Un firmware que lo incluye se distribuye bajo la GPL.

- Los parches de `patches/` se publican bajo GPL-2.0-or-later.
- El código fuente correspondiente a cada `.bin` publicado es: el commit de
  upstream fijado en `app.json` + los parches de esta carpeta, en el mismo
  commit de este repo que generó el release.
- El resto de las apps de este repo no se ven afectadas: cada carpeta de
  `apps/` es un programa independiente.

### El WAD no se incluye

El repo de upstream trae `iwad/doom1.c`, que es el DOOM1.WAD shareware de
id Software convertido a C. **No es GPL** y no se compila ni se redistribuye
acá: `build.sh` lo borra antes de compilar. Tenés que poner tu propio WAD en
la SD.

## Preparar el WAD

El motor de GBADoom usa un formato de WAD preprocesado. Convertilo en la PC:

```bash
git clone https://github.com/romalik/m5cardputer_doom
cd m5cardputer_doom/wad_converter
python3 gen_extralumps.py
gcc -O2 -o wadconverter wadconverter.c mus2mid.c extra_lumps.c
./wadconverter doom1.wad gdoom1.wad
```

Copiá `gdoom1.wad` a la microSD. Upstream probó `doom1.wad` (shareware) y,
con algunos problemas, `doom2.wad`.

**Copialo en una SD recién formateada (FAT32) o sin fragmentar.** El juego
mapea el archivo directo desde los sectores de la SD y no soporta archivos
fragmentados: si lo está, muestra "File fragmented" y reinicia.

## Instalar y jugar

1. Bajá `doom-v<versión>.bin` del release (o `sd-card.zip` de `sd-latest`) y
   copialo a la SD.
2. En M5Launcher: **SD → doom-v….bin → Install**.
3. Al arrancar, elegí el WAD convertido en el menú (`;` `.` para moverte,
   `enter` para elegir). La primera vez arma un caché en la flash y tarda.

| Acción | Tecla |
|---|---|
| Adelante / atrás | `;` / `.` |
| Girar izquierda / derecha | `l` / `'` |
| Disparar | `opt` |
| Strafe izq. / der. | `ctrl` / `alt` |
| Usar / abrir | `fn` |
| Mapa | `tab` |
| Armas | `1`–`7` |

Trucos: `iddqd` (dios), `idkfa` (todo), `idc<e><m>` (ir a E<e>M<m>, p. ej. `idc14`).

## Cardputer-Adv

El port original lee el teclado como una matriz de GPIO, que es como funciona
la Cardputer original. En la Adv el teclado lo maneja un **TCA8418** por I2C
(SDA G8, SCL G9, dirección `0x34`) y el parlante pasa por un códec **ES8311**
(`0x18`). El parche `0001-…` detecta la Adv probando el TCA8418 al arrancar.
Si responde, lee el teclado por I2C y enciende el ES8311. Si no, usa la matriz
GPIO como antes, así que el mismo `.bin` sirve para las dos.

## Estado

- Heredado de upstream: a veces se cuelga; guardar y cargar partida necesita
  SD; hay detalles menores con la música.
- El soporte de la Adv (teclado y audio) está escrito a partir de los drivers
  oficiales de M5Stack, pero **no está probado en hardware**.

## Actualizar upstream

Cambiá `upstream.ref` en `app.json`, corré `scripts/build-app.sh doom` y, si
algún parche no aplica, rehacelo sobre el commit nuevo. Subí `version` y
agregá la entrada en `CHANGELOG.md`.
