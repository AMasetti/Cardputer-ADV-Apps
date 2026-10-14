# Changelog

## [0.8.0]
- Rediseño de la pantalla de Seguimiento: la clasificación discreta
  "acercándose/estable/alejándose" (v1 con split de ventana cruda, v2 en
  0.7.1 con cruce de EMA) seguía siendo errática en uso real -- parado
  quieto al lado del propio dispositivo, alternaba entre los tres estados.
  Causa de fondo: cualquier umbral categórico sobre una señal con 10-15dBm
  de ruido normal de RF SIEMPRE va a tener casos límite que parpadean, no
  importa cuánto se ajuste la zona muerta -- no era un bug de calibración,
  era el enfoque equivocado.
- Reemplazado por una burbuja continua que crece/achica con la señal
  filtrada (una tercera EMA propia, `proximityFraction()`, más lenta que
  las de debug), estilo "Find Nearby" de Apple para AirTags: un anillo
  exterior fijo marca el "objetivo", la burbuja interior se acerca a su
  tamaño a medida que la señal mejora, y se pone verde al estar muy cerca.
  Sin estados discretos no hay nada que "parpadee" categóricamente -- en
  el peor caso la burbuja tiembla un poco, que es honesto (la señal de
  verdad tiembla).
- El número grande + sparkline + texto de tendencia se sacaron de la UI
  principal; el dBm crudo queda chico y abajo, solo para debug.
  `trend()`/`smoothedRssi()` en `room_sweep.h` ahora están documentados
  explícitamente como debug-only.

## [0.7.1]
- Fix: la tendencia en la pantalla de Seguimiento era errática (decía
  "acercándose" lejos del dispositivo y viceversa). Causa real: RSSI de
  BLE salta 10-15dBm aunque el dispositivo esté quieto y cerca (es física
  de RF normal, no un bug), pero el cálculo comparaba apenas 4 muestras
  crudas contra las otras 4 con una zona muerta de solo 3dBm -- un solo
  paquete ruidoso alcanzaba para voltear el resultado. Reemplazado por
  dos promedios móviles exponenciales (rápido/lento) y zona muerta sobre
  la diferencia entre ambos: hace falta una tendencia sostenida de varios
  paquetes para mover la aguja, no uno solo.
- El número grande (`smoothedRssi()`) ahora muestra la EMA rápida en vez
  de un promedio simple de ventana -- reacciona más parejo.

## [0.7.0]
- Nuevo: detección de accesorios Find My de Apple (AirTag y similares).
  Company ID de Apple (0x004C) solo no alcanza -- todo dispositivo Apple
  lo usa -- así que se capturó el byte de sub-protocolo del manufacturer
  data (`mfgTypeByte`) y se matchea `0x12` (Find My) específicamente.
  Categoría `FindMyTracker` nueva.
- Nuevo: dispositivos marcados como "mío" (tecla `t` en Detalle), con
  persistencia a SD (`/sentinel/trusted.txt`, sobrevive un reinicio). Un
  dispositivo propio no dispara alertas ni cuenta como sospechoso de
  convoy -- necesario porque tu propio AirTag está siempre con vos y por
  definición parecería "seguirte" igual que lo haría uno ajeno; la app
  solo es útil contra rastreo de terceros si le decís cuál es el tuyo.
  Se ordenan al final de la lista (ya los conocés, no urgen).
- Fingerprint ahora incluye `mfgTypeByte` para no colisionar distintos
  sub-protocolos de un mismo fabricante (ej. AirTag vs. AirPods, ambos
  Apple/0x004C).

## [0.6.1]
- Fix real: `category`/`confidenceLow` de un `Record` solo se calculaban
  en el primer paquete y nunca se actualizaban después. Muchos
  dispositivos mandan el nombre recién en el scan response, no en el
  primer advertising -- si ese primer paquete no traía nombre, el
  dispositivo quedaba clasificado como `Unknown` para siempre, aunque
  packets posteriores sí revelaran quién era. `resolve()` ahora mezcla
  el nombre/company ID del paquete de hoy con lo que el `Record` ya
  sabía de antes, y `sighting_store::record()` persiste la categoría
  recalculada en cada paquete, no solo al crear el registro.

## [0.6.0]
- Scroll en la lista: con más dispositivos que filas visibles, `cursorIndex`
  pasa a ser un índice absoluto sobre toda la lista (no solo las filas en
  pantalla) y la ventana visible se desliza para seguirlo. Scrollbar a la
  derecha (track + thumb) cuando hay más filas que lugar.
- Prioridad de orden actualizada: sospechosos de convoy, después
  **resultados identificados** (categoría real de SignatureDB -- son los
  interesantes, se destacan sobre lo no identificado aunque tenga mejor
  señal), recién ahí el resto por RSSI desc dentro de cada grupo.

## [0.5.0]
- Se reemplazó el radar por una pantalla de seguimiento honesta con lo
  que el RSSI realmente da: número grande + sparkline de las últimas
  muestras + texto de tendencia. El radar sugería dirección/posición que
  nunca tuvimos (ángulo fijo o no); esto es una sola dimensión y se lee
  como tal.
- Nuevo: tono de audio continuo opcional en la pantalla de seguimiento
  (frecuencia sube cuanto más cerca, estilo detector de metales), para
  buscar sin mirar la pantalla. Toggle con G0 corto (mantenido sigue
  siendo el mute global, sin cambios ahí).
- `Screen::Radar` renombrado a `Screen::Follow` / `drawRadar()` a
  `drawFollow()` en el código, para que el nombre no siga sugiriendo lo
  que ya no hace.

## [0.4.0]
- Nuevo: pantalla de Detalle entre la lista y el radar. Flujo ahora es
  Lista -> (enter) -> Detalle -> (enter) -> Radar, `del` vuelve un paso
  atrás en cada una.
- Detalle muestra fabricante (si hay firma conocida; heurística o
  "desconocido" si no), company id, nombre anunciado, RSSI actual, TX
  power declarado, tamaño de payload, veces visto, rotaciones de MAC,
  ventana temporal y score de ConvoyAnalyzer.
- `sighting_store::Record` ahora guarda el company id / TX power /
  payload length / nombre local del último advertising -- antes se
  resolvían y se perdían, no quedaban disponibles para mostrar después.

## [0.3.0]
- Nuevo: selección en la lista con el scan pausado (`;`/`.` mover,
  `enter` elegir) y pantalla de radar (`RoomSweep`) para seguir un
  dispositivo puntual vía RSSI -- ángulo fijo por fingerprint, solo la
  distancia al centro varía con la señal, con indicador de tendencia
  (acercándose/alejándose/estable). `del` vuelve a la lista.
- Nuevo: exportación a SD con la tecla `e` -- CSV + JSON del
  sighting_store actual, con hash SHA-256 de integridad al lado de cada
  archivo y un log de exports.
- Pendiente, no implementado todavía: WiFiSniffer (correlación BLE+WiFi)
  y soporte de RTC por Grove -- motivos documentados en la spec.

## [0.2.0]
- Nuevo: ConvoyAnalyzer (detección de seguimiento sostenido). Tecla `c`
  marca "cambié de lugar"; un dispositivo visto en 3+ lugares distintos
  (+ duración sostenida y/o ventana temporal larga) se marca `!SIGUE`,
  sube al tope de la lista y dispara un perfil de audio propio
  (`ConvoySuspect`), más agudo y con cooldown más corto.
- Fix: el cooldown de `AlertEngine` era solo por dispositivo, no por
  (dispositivo, perfil) — una alerta nueva podía quedar bloqueada por el
  cooldown de una alerta distinta para el mismo fingerprint.

## [0.1.2]
- Fix: la lista se reordenaba y parpadeaba en cada paquete de advertising
  (varias veces por segundo con varios dispositivos cerca). Render
  throttleado a ~300ms, sin perder ninguna actualización.
- Nuevo: `space` pausa/reanuda el scan (antes no había forma de detenerlo).

## [0.1.1]
- UI rehecha con el mismo lenguaje visual que `shell-rc`/`led-rc`: HUD
  arriba, chips de estado (audio/SD), barras de señal por dispositivo,
  franja de color por categoría, footer con atajos.
- Audio arranca en mute por defecto; se activa/desactiva con G0 mantenido
  (~0.8s), ya no con la tecla `m`.

## [0.1.0]
- Primera versión: scan BLE pasivo (observer-only, forzado a nivel de build).
- Fingerprint de dispositivo resistente a rotación de MAC (company ID +
  forma del payload + TX power + service UUIDs).
- SignatureDB inicial: smart glasses (Meta/Luxottica, Snap) por company ID,
  heurística de auriculares por nombre.
- Alertas de audio por categoría (sospechoso a volumen pleno, auricular a
  volumen bajo, confiado en silencio) con cooldown por dispositivo.
- Registro de avistamientos con log a SD, timestamp relativo al boot
  (sin RTC todavía -- pendiente módulo Grove).
- Pendiente: ConvoyAnalyzer, RoomSweepEngine, WiFiSniffer, ExportModule.
