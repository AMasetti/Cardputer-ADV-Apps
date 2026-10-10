# cardputer-apps

Monorepo de apps para la **M5Stack Cardputer-Adv**. Cada carpeta en `apps/` es una app con su código fuente y su propia versión. GitHub Actions compila solo las apps que cambiaron y publica un `.bin` por versión, listo para copiar a la microSD y abrir con **M5Launcher**.

```
apps/
  mp3-player/          # app propia (PlatformIO)
    app.json           # nombre, versión, builder
    CHANGELOG.md
    platformio.ini
    src/main.cpp
  meshtastic-ra02/     # firmware upstream + variante propia
    app.json           # versión + ref de meshtastic/firmware
    CHANGELOG.md
    build.sh           # clona upstream, copia variant/, compila
    variant/           # variant.h, pins_arduino.h, platformio.ini
    patches/           # (opcional) *.patch sobre upstream
  _template/           # base para apps nuevas
scripts/
  build-app.sh         # compila una app -> dist/<app>/<app>-v<ver>.bin
  changed-apps.sh      # qué apps cambiaron entre dos commits
  new-app.sh           # crea una app desde _template
docs/
  hardware.md          # cableado RA-02, SD, sensores I2C
.github/workflows/build.yml
```

## Apps

| App | Qué hace |
|---|---|
| `mp3-player` | Reproduce los `.mp3` de `/music` en la SD. `;` `.` lista · `enter` play · `space` pausa · `,` `/` anterior/siguiente · `-` `=` volumen · `s` shuffle |
| `doom` | Doom (port de [romalik](https://github.com/romalik/m5cardputer_doom) sobre GBADoom/PrBoom), con soporte para el teclado y el audio de la Adv. Necesita el WAD convertido en la SD. **GPL-2.0-or-later**, ver [apps/doom](apps/doom/README.md) |
| `snake` | Snake clásico. `;` `.` `,` `/` (o WASD) mover · `enter` jugar · `space` pausa · `m` sonido. Guarda el récord |
| `ekoparty-badge` | Logo de la Ekoparty 22 y QR a AMasetti.com. Cualquier tecla alterna a QR a pantalla completa |
| `meshtastic-ra02` | Meshtastic con un RA-02 (SX1278, 433 MHz) en el header EXT 14P, en lugar del Cap LoRa-1262. Cableado en [docs/hardware.md](docs/hardware.md) |

Cada app puede tener su propia licencia: si una carpeta de `apps/` trae un `LICENSE`, ese aplica a esa app (por ejemplo, `doom` es GPL-2.0-or-later).

## Versionado

- La versión de cada app vive en su `app.json` (semver) y se documenta en su `CHANGELOG.md` con una sección `## [x.y.z]`.
- **PR**: compila las apps tocadas como `<app>-v<ver>-dev.<sha>.bin` (artifact del run) y **falla si una app cambió sin subir la versión**.
- **Push a `main`**: compila las apps tocadas y crea el release `<app>-v<ver>` con el `.bin`, su `sha256`, `build-info.json` y las notas del CHANGELOG. Si el tag ya existe, no publica nada.
- Además actualiza el release **`sd-latest`** con `sd-card.zip`: el último `.bin` de cada app en `/apps`, para descomprimir directo en la SD.
- **Actions → build-apps → Run workflow** recompila todas (`all`) o una lista (`mp3-player,meshtastic-ra02`).

Si cambia algo en `scripts/` o en el workflow, se recompilan todas las apps.

## Cargar en la Cardputer

1. Instalá M5Launcher una vez (M5Burner o el flasher web del proyecto de bmorcelli).
2. Bajá el `.bin` del release, o `sd-card.zip` de `sd-latest`, y copialo a la microSD.
3. En el Launcher: **SD → elegí el `.bin` → Install**. Queda como app por defecto hasta que instales otra; para volver al menú, encendé apretando una tecla durante el splash.

Los `.bin` son **solo la partición de app** (`firmware.bin`), no el merged. Así no pisan el bootloader ni el Launcher. No los flashees por USB en offset 0x0.

> Tamaño: Meshtastic pesa bastante. Si el Launcher dice que no entra, cambiá el esquema de particiones en sus opciones. `build-app.sh` avisa si un binario pasa ~3.2 MB.

## Desarrollo local

```bash
pip install platformio
scripts/build-app.sh mp3-player              # -> dist/mp3-player/mp3-player-v0.1.0.bin
scripts/build-app.sh meshtastic-ra02 dev.1   # clona upstream en apps/meshtastic-ra02/.work
pio run -d apps/mp3-player -t upload         # flashea por USB directo (pisa el Launcher)
```

## App nueva

```bash
scripts/new-app.sh mi-app
# editá apps/mi-app/src/main.cpp, push -> CI publica mi-app-v0.1.0
```

Para mejorar una app: tocá su carpeta, subí `version` en `app.json`, agregá la sección al `CHANGELOG.md` y abrí un PR.

Para actualizar Meshtastic: cambiá `upstream.ref` en `apps/meshtastic-ra02/app.json` y subí la versión. Si upstream cambió la variante de Cardputer-Adv, compará con `variant/variant.h`.
