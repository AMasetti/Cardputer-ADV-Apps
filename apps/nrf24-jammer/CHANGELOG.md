# Changelog

## [0.2.0]
- Driver SPI propio sin dependencias externas (sin librería RF24).
- Fix compilación: vector.size() en vez de vector.length().
- Fix compilación: setFont() en vez de setTextFont() deprecado.
- Detecta módulo nRF24 y muestra OK/N/C.

## [0.1.0]
- Primera versión.
- Menú principal con modos: Bluetooth, Drones, WiFi, BLE, Zigbee, Misc y Settings.
- Jamming por portadora continua (Bluetooth, Drones, BLE data, Misc channel switching).
- Jamming por envío de paquetes (WiFi, BLE advertising, Zigbee, Misc packet sending).
- Navegación con teclado Cardputer.
