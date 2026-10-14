# BLE Sentinel — spec

Detector pasivo de dispositivos BLE (y más adelante WiFi) para contra-vigilancia
personal: identifica drones (Remote ID), smart glasses, trackers y otros
dispositivos por su advertising, sin anunciarse ni conectarse a nada.
App: `apps/ble-sentinel`.

## Principio rector

Todo lo que hace esta app es **escucha pasiva de advertising público**. No
transmite nada propio salvo lo mínimo indispensable del stack BLE (y eso
está deshabilitado a nivel de build, ver sección 7). No se conecta a
dispositivos de terceros, no explota nada, no interfiere con nada — es
equivalente, en lo que a RF respecta, a cualquier analizador BLE/WiFi
comercial (nRF Connect, Wireshark+adaptador, etc.).

## 1. Fingerprint más allá de la MAC

**Problema:** los dispositivos modernos rotan su MAC BLE (privacidad
estándar, ~15 min) para evitar tracking. Si identificás por MAC, perdés
continuidad cada vez que rota.

**Campos usados** (todos en el advertising packet, sin necesitar conexión):

| Campo | Fuente | Estabilidad |
|---|---|---|
| Company ID (manufacturer data) | AD type `0xFF`, primeros 2 bytes LE | Alta |
| Longitud total del payload | — | Alta |
| TX power declarado | AD type `0x0A` si está presente | Media-alta (bucketizado ±4dBm) |
| Service UUIDs anunciados | AD types `0x02/0x03/0x06/0x07` | Alta |

`fingerprint::compute()` combina esos campos en un hash (FNV-1a 64 bits,
truncado a 8 bytes — no es criptográfico, no hace falta: el objetivo es
diferenciar dispositivos, no resistir un atacante activo). Payload pobre
(sin manufacturer data ni service UUIDs) → `confidenceLow = true`, el match
sigue existiendo pero es menos confiable.

`isPlausibleMacRotation(deltaMs)` marca cuándo dos avistamientos con el
mismo fingerprint pero MAC distinta son compatibles con una rotación
normal (ventana 5-25 min) en vez de ruido/coincidencia.

Implementado: `src/fingerprint.h/.cpp`.

## 2. Detección de convoy / seguimiento sostenido

**Objetivo:** distinguir "hay un dispositivo cerca" de "me está siguiendo".

**Implementado**: `src/convoy_analyzer.h/.cpp`, cableado en `main.cpp`
(`processSightings`, tecla `c`, prioridad de orden + marcador `!SIGUE` en
la lista).

- `context_id` (en el código, `currentContext()`) es manual: el usuario
  marca "cambié de lugar" con la tecla `c`. No hay GPS en el Cardputer
  base — si se agrega ese módulo a futuro, lo natural es que dispare
  `markNewContext()` solo cuando el desplazamiento supera un umbral, sin
  tocar el resto de la API.
- `score()` por fingerprint suma: +40 si se vio en 3+ contextos distintos,
  +20 si el promedio de duración por contexto supera 5 min, +10 si la
  ventana total entre el primer y el último avistamiento supera 1h.
  `isSuspect()` es `score() >= 60`.
- **Pendiente (0 puntos hoy):** correlación entre la tendencia de RSSI y el
  movimiento propio del usuario (necesitaría acelerómetro o GPS). Sin ese
  factor, el máximo posible es 70 — alcanza para cruzar el umbral con "3+
  contextos" solo, o con "3+ contextos" + cualquiera de los otros dos; un
  solo factor aislado nunca dispara la alerta.
- Un sospechoso de convoy dispara `AlertProfile::ConvoySuspect` (audio más
  agudo, más beeps, cooldown más corto que el resto) y dentro de
  `alert_engine` el cooldown quedó corregido para ser por (dispositivo,
  perfil) y no solo por dispositivo — si no, una alerta de convoy para un
  fingerprint que ya había disparado `SuspiciousDefault` hacía poco
  quedaba bloqueada por el cooldown de esa otra alerta.

## 3. Modo "barrido de ambiente" (room sweep)

**Objetivo:** localizar físicamente un dispositivo ya detectado (buscar
cámara/mic oculto en un cuarto).

**Diseño (pendiente):** modo dedicado que filtra el scan a un solo
`fingerprint_hash`, lee RSSI a alta frecuencia (~200-500ms), y muestra un
indicador relativo tipo "más cerca / más lejos" (no es triangulación real,
es gradiente de RSSI — hay que dejarlo claro en la UI para no generar falsa
confianza, dado el ruido de multipath en interiores).

No implementado en v1.

## 4. Correlación BLE + WiFi

**Objetivo:** cubrir dispositivos que usan WiFi (hotspot de cámaras/drones)
en vez de o además de BLE.

**Diseño (pendiente):** `WiFiSniffer` en modo promiscuo (`esp_wifi_set_promiscuous`,
sin asociarse a ninguna red) leyendo beacon frames y probe requests. Mismo
OUI visto en BLE y WiFi en la misma ventana → se linkean como "dispositivo
dual-radio". SSID con prefijos conocidos de hotspot de cámara/drone →
categoriza directo sin necesitar BLE.

**Nota de diseño:** correr WiFi promiscuo y BLE scan en simultáneo en el
mismo ESP32-S3 compite por tiempo de radio — probablemente haga falta
time-slicing (alternar ventanas de 2-3s por radio) en vez de concurrencia
real. A validar en el prototipo.

No implementado en v1.

## 5. "Primera vez visto vs. recurrente"

**v1 implementado, con una limitación explícita:** sin RTC (ver sección 8),
todo timestamp es relativo al boot de la sesión actual (`millis()`). Esto
funciona bien *dentro* de una sesión (podés ver que algo lleva 20 minutos
apareciendo) pero **no compara entre sesiones separadas por un apagado** —
cada boot es `session_id` nuevo sin noción de cuánto tiempo pasó desde el
anterior. "Recurrente, mismo lugar, hace 3 días" queda pendiente hasta que
se agregue el RTC.

Implementado: `src/sighting_store.h/.cpp` (`Record`, `SightingKind`,
log append-only a SD en `/sentinel/history/session_<id>.log`, formato JSON
por línea para que un `ExportModule` futuro o herramientas externas lo
puedan leer directo).

## 6. Exportación como registro/evidencia

**Diseño (pendiente de implementar como módulo propio — v1 ya deja el log
en un formato consumible):**
- CSV + JSON con hash SHA-256 del archivo generado al lado (`.sha256`), más
  un log de exports (`/sentinel/export_log.txt`) con timestamp+filename+hash.
- **Limitación a documentar en la UI:** esto es integridad básica (detecta
  modificación posterior), no es firma criptográfica con clave privada ni
  timestamp de autoridad certificada. Si el uso real es legal, la app
  facilita el registro — no sustituye asesoría legal sobre admisibilidad.

No implementado en v1 como módulo de export; el log base que necesita ya
existe (`sighting_store` escribe a SD en formato JSON por línea).

## 7. Auto-ocultamiento del propio Cardputer

**v1 implementado en dos capas:**

1. **Build-time** (`apps/ble-sentinel/platformio.ini`):
   ```
   -D CONFIG_BT_NIMBLE_ROLE_PERIPHERAL_DISABLED
   -D CONFIG_BT_NIMBLE_ROLE_BROADCASTER_DISABLED
   -D CONFIG_BT_NIMBLE_ROLE_CENTRAL_DISABLED
   ```
   El firmware no puede anunciarse ni aceptar/iniciar conexiones BLE aunque
   algún código lo intente — el rol queda fuera del binario.
2. **Runtime** (`src/ble_scanner.cpp`): `NimBLEDevice::init("")` con nombre
   vacío, y el wrapper nunca llama a métodos de advertising/conexión.

**Pendiente:** cuando se agregue `WiFiSniffer` (punto 4), el modo
promiscuo de por sí no transmite — pero hay que auditar que no quede
ningún softAP ni intento de `esp_wifi_connect` activo en el mismo binario.

## 8. RTC (decisión tomada, no implementado aún)

Se agregará el **Unit RTC (U126, chip HYM8563)** de M5Stack por Grove/I2C
más adelante. Mientras tanto, v1 usa timestamps relativos al boot (ver
sección 5). El cambio futuro es aditivo: un campo `wallClockMs` opcional en
`Record`, poblado solo si se detecta el RTC por I2C probe a `0x51` en boot
— no requiere tocar el código ya escrito.

## Alertas de audio (AlertEngine)

Implementado: `src/alert_engine.h/.cpp`. Perfiles v1:

| Categoría | Volumen (% del master) | Tono | Patrón |
|---|---|---|---|
| Sospechoso (default — sin match, o match no verificado) | 100% | 1200Hz | 2 beeps |
| Auricular/headset (heurística por nombre) | 35% | 800Hz | 1 beep |
| Confiado/baseline | 0% (silencioso) | — | — |

Cooldown de 60s por `fingerprint_hash` para evitar que un dispositivo fijo
alerte sin parar mientras sigue a la vista. Toggle general (`m`) y volumen
master (`+`/`-`) desde teclado en v1; UI de configuración más completa
queda para después.

## SignatureDB — estado de las firmas

Ver `src/signature_db.cpp`. Company IDs de smart glasses tomados de los
proyectos open-source NoPeek y "Nearby Glasses" (no de captura propia
todavía) — `0x0D53` (Luxottica) y `0x058E` (Meta Platforms Technologies)
están marcados `verified = true` porque ambos proyectos coinciden; `0x01AB`
y `0x03C2` quedan `verified = false` porque hay discrepancia entre fuentes
o no se confirmaron de forma independiente. Cualquier entrada no verificada
sube `confidenceLow` en el `Record` resultante, visible en la UI con un `?`.

**Pendiente:** Remote ID de drones (ASTM F3411, matcheo por service UUID,
no por company ID — el UUID exacto no se confirmó en la investigación
previa, hay que validarlo contra `opendroneid-core-c` antes de implementar
el matcher). Firmas de Unitree, GoPro, dashcams, trackers (AirTag/Tile/
SmartTag) — todas pendientes de captura real o de portar desde un proyecto
open-source equivalente, mismo criterio que con NoPeek.
