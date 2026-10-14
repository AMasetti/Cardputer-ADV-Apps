# Changelog

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
