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

**Implementado** en `src/room_sweep.h/.cpp` + `drawFollow()` en
`main.cpp`. El flujo real quedó en 3 pantallas, no 2: con el scan
pausado, `;`/`.` mueve la selección en la lista y `enter` abre una
**pantalla de Detalle** (fabricante, company id, nombre anunciado, RSSI,
TX power, payload, veces visto, rotaciones de MAC, score de convoy --
ver sección de `sighting_store` abajo) antes de llegar al seguimiento.
Desde Detalle, `enter` entra a la pantalla de seguimiento (que retoma el
scan si estaba pausado, porque necesita RSSI en vivo); `del` siempre
vuelve un paso atrás (Seguimiento→Detalle, Detalle→Lista).

**La primera versión era un radar y se descartó** (quedó en el historial
de `main.cpp` como referencia de lo que no hacer): un punto en un disco
con ángulo fijo por fingerprint sigue leyéndose como "ahí está", aunque
el ángulo no significara nada. RSSI es una sola dimensión (distancia
aproximada) -- darle forma de mapa 2D es la interfaz engañosa, no la
honesta, por más cartel de disclaimer que tenga encima. Se reemplazó por:

- **v1 (número grande + sparkline + texto de tendencia) y v2 (0.7.1,
  cruce de EMA) se descartaron -- ambas erráticas en uso real, no solo
  bugs de calibración.** v1 comparaba la primera mitad contra la segunda
  mitad de la ventana cruda de 8 muestras con zona muerta de 3dBm; un
  solo paquete ruidoso alcanzaba para voltear el resultado. v2 reemplazó
  eso por dos EMA (alpha 0.35 rápida / 0.06 lenta) con zona muerta de
  2.5dBm sobre la diferencia entre ambas -- mejoró pero el usuario
  reportó que **seguía alternando entre "acercándose/estable/alejándose"
  parado quieto al lado del propio dispositivo**. Diagnóstico de fondo:
  RSSI de BLE real salta 10-15dBm aunque el dispositivo esté quieto y
  cerca (multipath, orientación de antena, el propio cuerpo bloqueando
  la señal -- física de RF normal, no un bug), y **cualquier umbral
  categórico sobre una señal así de ruidosa siempre va a tener casos
  límite que parpadean**, sin importar cuánto se ajuste la zona muerta o
  qué tan lentos sean los filtros. Ajustar el threshold una tercera vez
  no iba a arreglar un problema de forma, no de calibración.
- **v3 (0.8.0): burbuja continua en vez de clasificación
  discreta**, estilo "Find Nearby" de Apple para AirTags. Una tercera EMA
  propia (`proximityFraction()` en `room_sweep.cpp`, alpha 0.05,
  desacoplada de las EMA de debug) se mapea linealmente a un rango de
  RSSI. En `drawFollow()`: un anillo exterior fijo marca el
  "objetivo" (radio máximo), y un círculo interior relleno cuyo radio
  mostrado se desliza (lerp, factor 0.25 por frame a 60ms) hacia
  `maxR * proximityFraction()` -- la animación se reinicia de golpe si
  cambia el `followedHash()` seguido, para no animar desde una sesión
  vieja. El círculo se pone verde (`C_ON`) cuando `proximityFraction() >
  0.85` como señal de "lo encontraste", igual que el "Find Nearby" de
  Apple. Sin estados discretos no hay nada que clasificar mal -- en el
  peor caso la burbuja tiembla un poco en vez de afirmar categóricamente
  algo que puede estar mal, y eso es honesto (la señal de verdad tiembla).
  El número grande, el sparkline y el texto de tendencia se sacaron de la
  UI principal; `smoothedRssi()` y `trend()` siguen existiendo pero
  documentados como debug-only (el dBm crudo queda chico y abajo de la
  burbuja, solo de referencia).
- **Rango de mapeo recalibrado en 0.8.1 -- bug real, no cosmético.** v3
  mapeaba inicialmente el rango teórico de BLE (-95dBm lejos / -40dBm
  cerca). En hardware real (Cardputer + ESP32-S3) ese rango nunca se
  alcanza -- el usuario reportó -60dBm parado al lado del dispositivo y
  -80dBm lejos, muy lejos de los extremos teóricos -- así que la burbuja
  nunca llegaba a llenarse ni a vaciarse del todo, quedaba casi siempre a
  mitad de tamaño sin importar la distancia real (el mismo tipo de "se ve
  mal pero el filtro no tiene la culpa" que afectó a v1/v2, esta vez en el
  rango de mapeo en vez de en la clasificación). `BUBBLE_RSSI_MIN`/`_MAX`
  en `room_sweep.cpp` recalibrados a -85dBm/-55dBm (margen de 5dBm sobre
  lo reportado, para no pegar en el tope/piso con el primer paso). Sigue
  siendo una calibración a ojo con un solo reporte de campo, no medida
  con un set de dispositivos/distancias -- candidata a reajuste si otros
  dispositivos (con TX power/antena distinta) se sienten igual de
  comprimidos.
- **Tono de audio continuo opcional** (frecuencia sube cuanto más cerca,
  300-1800Hz mapeado al mismo rango calibrado que la burbuja -- antes
  tenía su propio rango hardcodeado -95/-40dBm, desincronizado de la
  burbuja desde 0.8.1), para buscar sin mirar la pantalla -- estilo
  detector de metales. Toggle con **G0 corto** (soltado antes del
  mantenido); G0 mantenido sigue siendo el mute global, sin cambios ahí.
  El flanco de bajada se detecta a mano comparando contra el estado del
  tick anterior, no con un método `wasReleased()` de la librería que no
  se usó en ningún otro lado del repo todavía.
- **Limitación conocida, no resuelta:** el tono continuo y los beeps
  discretos de `AlertEngine` comparten el mismo canal de speaker. Si un
  `ConvoySuspect` u otra alerta dispara mientras el tono está sonando,
  pueden pisarse entre sí por un instante. No es un crash, es un glitch
  de audio cosmético -- se deja documentado en vez de armar un mezclador
  de prioridad de canal único para esto todavía.
- `del` vuelve a Detalle (no a la lista -- se llegó al seguimiento desde
  ahí).

## 4. Correlación BLE + WiFi

**Objetivo:** cubrir dispositivos que usan WiFi (hotspot de cámaras/drones)
en vez de o además de BLE.

**Diseño (pendiente, no implementado):** `WiFiSniffer` en modo promiscuo
(`esp_wifi_set_promiscuous`, sin asociarse a ninguna red) leyendo beacon
frames y probe requests. Mismo OUI visto en BLE y WiFi en la misma ventana
→ se linkean como "dispositivo dual-radio". SSID con prefijos conocidos de
hotspot de cámara/drone → categoriza directo sin necesitar BLE.

**Por qué sigue sin implementar:** correr WiFi promiscuo y BLE scan en
simultáneo en el mismo ESP32-S3 compite por tiempo de radio y probablemente
necesite time-slicing en vez de concurrencia real -- es una decisión de
diseño que hay que validar en el prototipo real, no algo para resolver a
ciegas en una sesión sin el hardware delante. Además, el `platformio.ini`
actual deshabilita a propósito los roles NimBLE que no sean observer
(sección 7) como garantía de que este binario no se puede volver
"ruidoso" por error; sumar WiFi promiscuo es un subsistema de radio
distinto y merece su propio build/validación antes de mezclarlo con eso.

**Limitación relacionada, real y sin solución fácil:** varios auriculares
(ej. Beats, que usan los mismos chips W1/H1 que AirPods) transmiten el
audio por **Bluetooth Classic (A2DP)**, no BLE. Mientras están conectados
a un teléfono reproduciendo audio, típicamente no tienen razón para seguir
anunciando por BLE -- ya están conectados, no necesitan ser descubiertos.
`BLEScanner` es un observer de BLE únicamente (NimBLE); Bluetooth Classic
es una pila de radio distinta (Bluedroid en el ESP32, no NimBLE) y correr
ambas a la vez tiene la misma clase de problema de coexistencia que el
WiFiSniffer de arriba, pero peor: históricamente en el ESP32 no se pueden
inicializar NimBLE y Bluedroid Classic al mismo tiempo sin reconfiguración
de bajo nivel. Por eso un dispositivo así **puede no aparecer nunca** en
la lista mientras está conectado por Classic -- no es un bug de matching,
es que la app directamente no escucha esa capa. Queda fuera de alcance por
ahora, mismo criterio que WiFiSniffer: no se resuelve a ciegas sin
hardware para validar la coexistencia de radios.

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

**Bug real encontrado y corregido (v0.6.1):** `category`/`confidenceLow`
se calculaban una sola vez, en el primer paquete, y nunca se
actualizaban. Muchos dispositivos mandan el nombre local recién en el
scan response, no en el advertising inicial -- si ese primer paquete no
traía nombre, el `Record` quedaba `Unknown` para siempre, aunque
paquetes posteriores sí revelaran una identificación (ej. un auricular
que recién después manda "AirPods"). `resolve()` en `main.cpp` ahora
mezcla el company id/nombre del paquete de hoy con lo que el `Record` ya
sabía (una vez identificado, no se "olvida" por un paquete sin nombre), y
`record()` persiste la categoría recalculada en cada paquete, no solo al
crear el registro.

**Bug real encontrado y corregido (v0.9.0): el nombre anunciado no se
mostraba en la lista principal.** `category`/`confidenceLow` sí se
recalculaban en cada paquete (fix de arriba), pero eso solo decide la
franja de color -- el *label* de la fila en `drawList()` seguía siendo
`categoryLabel(r->category)` a secas, y un dispositivo sin firma conocida
en `SignatureDB` (ej. un reloj Casio que anuncia BLE pero no matchea
ningún company id/heurística) siempre caía en `"unknown"`, aunque
`r->lastLocalName` ya tuviera el nombre real -- visible en Detalle, pero
nunca en la lista. Ahora el label usa `lastLocalName` cuando hay uno
(hasta 12 caracteres, truncado), y solo cae a `categoryLabel()` si no hay
nombre. El color/franja de categoría no cambia -- "sabemos el nombre" y
"sabemos qué tipo de dispositivo es" son cosas distintas, un nombre
anunciado no verifica nada por sí solo. Como el label se lee de
`lastLocalName` en cada render (no se cachea), si el nombre llega recién
en una vuelta de scan posterior, se corrige solo en el próximo
redibujado.

**Nuevo (v0.9.0): log a SD ahora es opt-in, apagado por defecto.** Antes
`sighting_store::begin()` abría el log de historial y grababa todo evento
`New`/`MacRotated` automáticamente en cuanto había una tarjeta SD montada
-- sin forma de desactivarlo ni de elegir cuándo grabar. Grabar
continuamente no debería ser el comportamiento implícito solo porque hay
una tarjeta puesta: ahora `setLoggingEnabled()`/`loggingEnabled()`
(`sighting_store.h`) gatean la escritura real, con la tecla `l` como
toggle desde `main.cpp` y arrancando en `false` siempre. El chip de
estado en el HUD (antes "SD OK"/"SD --") ahora distingue "hay tarjeta"
de "estoy grabando" ("LOG ON"/"LOG OFF"/"SD --"). Apagar el log es solo
sobre el archivo -- no toca `records` en RAM ni nada del resto del store.

**Nuevo (v0.9.0): la lista principal solo muestra dispositivos
"activos"** (un paquete nuevo en los últimos 10s, `ACTIVE_TIMEOUT_MS` en
`main.cpp`). Antes la lista acumulaba todo lo visto en la sesión -- un
dispositivo del que ya te alejaste hace minutos seguía ahí para siempre,
mezclado con lo que de verdad está cerca ahora. El filtro vive en
`sortedOrder()` (compartido por `drawList()` y la navegación por
teclado) y es **puramente de UI**: no borra ni oculta nada de
`sighting_store` (`records` sigue el Record toda la sesión, lo necesitan
Detalle/Seguimiento), y el log a SD -- si está prendido -- sigue
registrando todo lo que pasa por `record()` exactamente igual, visible en
la lista o no; un dispositivo puede desaparecer de la pantalla sin dejar
de loggearse, son cosas independientes. El filtro **no se aplica con el
scan pausado**: pausar es justamente para congelar la lista y elegir algo
de ahí (`;`/`.` + `enter`), y como `lastSeenMs` no avanza para nadie
mientras está pausado, aplicar el timeout ahí haría que la lista se
vaciara sola mientras la estás mirando -- lo opuesto de para qué se
pausa.

## 6. Exportación como registro/evidencia

**Implementado:** `src/export_module.h/.cpp`, tecla `e` desde la lista.

- Vuelca el `sighting_store` actual a `/sentinel/exports/export_s<ses>_<n>.csv`
  y `.json` (mismo contenido, JSON con metadata de sesión).
- Cada archivo tiene un `.sha256` al lado (hash calculado con la API
  genérica de mbedtls, `mbedtls_md_*`, incremental para no cargar el
  archivo entero en RAM) y una línea en `/sentinel/export_log.txt` con
  `{ts_rel_ms, file, sha256}`.
- **Limitación documentada, no silenciada:** esto es integridad básica
  (detecta modificación posterior), no es firma criptográfica con clave
  privada ni timestamp de autoridad certificada. El JSON exportado incluye
  `"timestamp_type":"relative_since_boot"` explícito por la misma razón que
  el resto de la app (sección 5) -- si el uso real es documentar algo
  serio, esto ayuda pero no sustituye asesoría legal sobre admisibilidad.
- **No validado en hardware real todavía:** la API `mbedtls_md_*` es la
  más estable entre versiones de mbedtls (evita la saga de renombres de
  `mbedtls_sha256_starts`/`_ret`), pero no se compiló en este entorno
  (sin toolchain PlatformIO) -- si el CI la rechaza, es la primera
  sospechosa junto con el modo de apertura de archivo para el log
  append (`FILE_WRITE` reabierto en cada export, asumiendo que el core
  de Arduino-ESP32 lo posiciona al final del archivo en vez de truncar).

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
| Fabricante identificado (registro general, ver sección SignatureDB) | 0% (silencioso) | — | — |
| Confiado/baseline | 0% (silencioso) | — | — |

Cooldown de 60s por `fingerprint_hash` para evitar que un dispositivo fijo
alerte sin parar mientras sigue a la vista (más el perfil `ConvoySuspect`,
cooldown de 30s, ver sección 2). Toggle general con **G0 mantenido**
(arranca en mute) y volumen master con `+`/`-` desde teclado; UI de
configuración más completa queda para después.

## UI — lista, selección y orden

Implementado en `main.cpp`. La lista (`drawList()`/`sortedOrder()`) y la
navegación por teclado comparten estado:

- **Orden de prioridad**, de más a menos urgente: 1) sospechosos de
  convoy, 2) resultados **identificados** (`isIdentified()`, ver abajo),
  3) todo lo demás. Dentro de cada grupo, por RSSI desc, con un
  **tiebreak final por orden de inserción** (primera vez visto en la
  sesión) agregado en v0.10.0 -- ver bug real abajo.
- **"Identificado" ampliado en v0.10.1:** ya no es solo `category !=
  Unknown` (tener una categoría real de SignatureDB) -- un dispositivo
  que anuncia su propio nombre (ej. AirPods) también cuenta, aunque no
  matchee ninguna firma. Antes de este fix, un dispositivo con nombre
  pero `category == Unknown` se mostraba blanco y ordenaba junto con los
  unknowns reales por RSSI nomás -- el usuario lo marcó como incorrecto:
  si ya sabemos cómo se llama, no tiene sentido tratarlo igual que algo
  de lo que no sabemos nada. `isIdentified(const Record*)` en `main.cpp`
  ahora es `category != Unknown || lastLocalName.length() > 0`, y
  `categoryColor()` toma el mismo criterio para pintar verde. El audio no
  cambió: `Unknown` sigue siendo `SuspiciousDefault` tenga nombre o no --
  un nombre anunciado no prueba nada, lo podría poner cualquiera.
- **Scroll**: con el scan pausado, `cursorIndex` es un índice absoluto
  sobre toda la lista ordenada, no solo sobre las filas que entran en
  pantalla (`MAX_VISIBLE_ROWS`, hoy 8). `drawList()` calcula una ventana
  (`scrollOffset`) que se desliza para que la selección siempre esté
  visible, con una scrollbar (track + thumb) a la derecha cuando hay más
  filas que lugar.
- Mientras el scan sigue corriendo, la selección no existe (la lista se
  reordena sola con cada paquete) y siempre se ve desde el principio.
- **Solo dispositivos "activos"** (v0.9.0): `sortedOrder()` filtra a los
  vistos en los últimos `ACTIVE_TIMEOUT_MS` (10s), pero solo mientras el
  scan corre -- pausado no, porque `lastSeenMs` no avanza para nadie y
  aplicar el timeout ahí vaciaría la lista sola mientras la estás
  mirando. Es un filtro de UI nomás: no afecta `sighting_store` (sigue el
  Record toda la sesión) ni el log a SD si está prendido.
- **Label de la fila**: nombre anunciado > fabricante (match del
  registro general de company IDs) > categoría genérica (v0.10.0, ver
  sección SignatureDB).

**Bug real corregido (v0.10.0): la lista se reordenaba sin motivo.** El
comparador de `sortedOrder()` terminaba en "por RSSI desc" sin ningún
criterio para romper empates. RSSI real salta varios dBm por paquete
(misma física de RF que afecta a `room_sweep`, ver sección 3) -- dos
dispositivos con RSSI igual, o que se cruzan entre un paquete y el
siguiente, podían volver de `std::sort` en cualquier orden relativo cada
vez (`std::sort` no garantiza estabilidad). Antes de v0.9.0 era invisible
porque todo lo no identificado decía "unknown" igual, no había forma de
notar cuál era cuál; en cuanto 0.9.0 empezó a mostrar el nombre real en
la fila, ese reordenamiento sin sentido se volvió visible, reportado como
"los dispositivos con nombre van de arriba a abajo". Fix: tiebreak final
por índice de inserción (orden en que se vio por primera vez cada
fingerprint en la sesión) -- determinístico, no depende de que el
algoritmo de sort sea estable.

## SignatureDB — estado de las firmas

Ver `src/signature_db.cpp`. Company IDs de smart glasses tomados de los
proyectos open-source NoPeek y "Nearby Glasses" (no de captura propia
todavía) — `0x0D53` (Luxottica) y `0x058E` (Meta Platforms Technologies)
están marcados `verified = true` porque ambos proyectos coinciden; `0x01AB`
y `0x03C2` quedan `verified = false` porque hay discrepancia entre fuentes
o no se confirmaron de forma independiente. Cualquier entrada no verificada
sube `confidenceLow` en el `Record` resultante, visible en la UI con un `?`.

**Registro general de fabricantes — nuevo en v0.10.0.** Hasta 0.9.0,
`TABLE[]` solo tenía las 4 firmas de smart glasses -- cualquier otro
company ID, aunque fuera perfectamente identificable, caía en
`Category::Unknown`. Se agregó un subconjunto curado a mano (~60
entradas) del registro **oficial** de company identifiers del Bluetooth
SIG (no un proyecto de terceros reconstruyéndolo -- la fuente es la
autoridad que asigna los IDs, así que estas entradas van `verified =
true`): electrónica de consumo reconocible (Samsung, Google, Microsoft,
Garmin, Sony, Bose, Harman/JBL, Casio, Seiko, Sonos, routers/laptops
comunes, etc.) bajo `Category::IdentifiedVendor`, más dos extensiones a
categorías ya existentes:
- **Cámaras de acción** (GoPro, Insta360/Arashi Vision) suman al mismo
  bucket que smart glasses (`Category::SmartGlasses`) -- el criterio es
  "diseñado para grabar", no "tiene forma de anteojos".
- **Robots** (iRobot, Anki, Ninebot/Segway) suman al mismo bucket que
  drones (`Category::DroneRemoteId`) -- "algo autónomo/teleoperado
  cerca", aunque un Roomba no vuele.

No es el registro completo (~4000 entradas, mayormente integradores
B2B/industriales sin relevancia para detectar vigilancia) -- es un
subconjunto elegido a mano de lo más reconocible para uso personal. El
label de la lista principal ahora cae a `vendorLabel` de este registro
cuando el dispositivo no anunció nombre propio (antes, sin nombre, era
siempre "unknown" aunque el company ID fuera identificable).

**Código de colores — nuevo en v0.10.0**, pedido explícitamente por el
usuario, reemplaza la paleta anterior (rojo=sospechoso / gris=auricular /
ámbar=sin verificar):

| Color | Categoría |
|---|---|
| Blanco | `Unknown` **sin nombre anunciado tampoco** -- no sabemos nada de verdad |
| Azul | `SmartGlasses` (incluye cámaras de acción) -- "meta o filmación" |
| Violeta | `DroneRemoteId` (incluye robots) -- "drones y robots" |
| Rojo | `FindMyTracker` |
| Verde | `Headset`, `IdentifiedVendor`, o `Unknown` **con nombre anunciado** (v0.10.1) -- "otro dispositivo identificado", todo tranquilo |

**Cambio de comportamiento visual deliberado:** antes `Unknown` se
pintaba rojo ("sin match -> tratado como sospechoso por defecto", la
postura conservadora del AlertEngine). Ahora es blanco/neutro. Esto es
**solo visual** -- el audio no cambió, `Category::Unknown` sigue
resolviendo a `AlertProfile::SuspiciousDefault` en `resolve()`
(`main.cpp`), así que un dispositivo sin identificar sigue sonando a
volumen pleno igual que antes, solo que ya no se ve rojo en la lista. Los
nuevos matches de `IdentifiedVendor` usan `AlertProfile::Silent` a
propósito -- no tiene sentido que un Garmin o un router al lado suenen
igual que algo real sin identificar.

**Find My de Apple (AirTag y otros accesorios) — implementado en v0.7.0.**
Company ID 0x004C (Apple) **no alcanza solo**: todo dispositivo Apple
(iPhone, AirPods, Watch, continuity) lo usa para cosas distintas. El campo
que distingue el protocolo es el byte inmediatamente después del company
ID dentro del manufacturer data ("type byte" en la terminología de Apple
para sus distintos sub-protocolos sobre 0x004C) — `0x12` identifica Find
My específicamente. Esto obligó a capturar ese byte en `ble_scanner.h`
(`mfgTypeByte`, antes no se guardaba) y sumarlo al fingerprint
(`fingerprint::AdvInput::mfgTypeByte`) para que un AirTag y un AirPods no
colisionen en el mismo fingerprint por compartir company ID.

- **Fuente:** reverse engineering público (Adam Catley, y el paper
  arXiv 2401.13584 sobre seguridad de AirTag/SmartTag), no documentación
  oficial de Apple. Coinciden en el layout general; hay discrepancia
  entre fuentes sobre el offset/significado exacto del status byte que
  distingue "cerca de su dueño" de "separado/perdido" -- v1 no intenta
  decodificar eso, solo reporta "hay un accesorio Find My anunciando
  cerca" (`Category::FindMyTracker`), que alcanza para el caso de uso de
  detectar trackers ajenos.
- **No afirma "es un AirTag"** puntualmente: otros accesorios Find My
  (de terceros, certificados MFi) comparten el mismo type byte. Es
  honesto llamarlo "accesorio Find My", no "AirTag" sin más.
- Matcheo vive en `signature_db::matchFindMyNetwork(companyId, mfgTypeByte)`,
  separado de la tabla `TABLE[]` por company ID porque necesita un segundo
  campo -- no es una `Entry` más.

**Pendiente:** Remote ID de drones (ASTM F3411, matcheo por service UUID,
no por company ID — el UUID exacto no se confirmó en la investigación
previa, hay que validarlo contra `opendroneid-core-c` antes de implementar
el matcher). Firmas de Unitree, GoPro, dashcams, Tile/SmartTag (formato
propio de Samsung/Tile, no Find My) — todas pendientes de captura real o
de portar desde un proyecto open-source equivalente, mismo criterio que
con NoPeek.

## Dispositivos "propios" (trusted)

**Implementado en v0.7.0.** Tecla `t` en la pantalla de Detalle marca o
desmarca el dispositivo seleccionado como propio. Persiste a
`/sentinel/trusted.txt` (lista de fingerprint hashes en hex, uno por
línea, reescrita completa en cada cambio) si hay SD -- sobrevive un
reinicio, a diferencia del resto del estado de la sesión, porque es
justo lo que no se quiere tener que re-marcar cada vez.

**Por qué existe:** `ConvoyAnalyzer` (sección 2) detecta "visto en 3+
lugares distintos" como señal de seguimiento -- pero un AirTag/tracker
que el usuario lleva siempre en su propia mochila cumple esa condición
por definición, en cada sesión, para siempre. Sin una forma de decir
"este es mío", la app alertaría constantemente sobre el propio
dispositivo del usuario, que es ruido, no señal. El caso de uso real
(avisar sobre un tracker *ajeno* que empieza a mostrar el mismo patrón)
solo funciona si el usuario puede excluir el suyo.

Un dispositivo `trusted`:
- No dispara `alertengine::trigger()` (ni el perfil de categoría ni
  `ConvoySuspect`), aunque `convoyanalyzer::score()` siga calculándose
  normalmente (se puede ver en Detalle, solo no alerta).
- Se ordena al final de la lista (`sortedOrder()`), por debajo incluso
  de lo no identificado -- ya se sabe qué es, no urge mirarlo.
- Se muestra en verde con la etiqueta "mio" en vez de la categoría o de
  "!SIGUE", tanto en la lista como en Detalle (`[MIO]` junto al label).
