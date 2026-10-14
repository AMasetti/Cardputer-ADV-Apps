# Changelog

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
