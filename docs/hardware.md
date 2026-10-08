# Hardware

## RA-02 (SX1278) en el header EXT 2.54-14P de la Cardputer-Adv

Mismos pines que usa el Cap LoRa oficial, así que no hay conflictos con display, teclado ni SD.

| RA-02 | Cardputer-Adv |
|---|---|
| SCK | G40 |
| MISO | G39 |
| MOSI | G14 |
| NSS | G5 |
| RST | G3 |
| DIO0 | G4 |
| 3.3V | 3V3 (**nunca 5V**) |
| GND | GND |

- DIO1–DIO5 sin conectar.
- Conectá la antena al IPEX **antes** de transmitir.
- El bus SPI (G40/G39/G14) es compartido con la microSD (CS G12). Funciona porque cada uno tiene su CS.
- El RA-02 es de 433 MHz: configurá `lora.region EU_433`. En Argentina el tráfico Meshtastic habitual está en 915 MHz, así que en 433 la red queda aislada de hecho. Revisá los límites de potencia de ENACOM para esa banda.
- El firmware intenta inicializar el switch de RF del Cap (expansor I2C 0x43). Sin el Cap solo deja un `Cap LoRa-1262 not found` en el log; es inofensivo.

## Red privada y sin relay de mensajes ajenos

```bash
meshtastic --set lora.region EU_433
meshtastic --set lora.channel_num 3
meshtastic --ch-set name "nexus" --ch-set psk random --ch-index 0
meshtastic --set device.rebroadcast_mode KNOWN_ONLY
meshtastic --set mqtt.enabled false
```

Para un nodo que no tiene que rutear nada: `meshtastic --set device.role CLIENT_MUTE`.

## Sensores I2C

El Grove HY2.0-4P queda libre: SDA G2, SCL G1 (`I2C_SDA1`/`I2C_SCL1` en la variante). Los sensores soportados por el módulo de Telemetry de Meshtastic se detectan solos:

```bash
meshtastic --set telemetry.environment_measurement_enabled true
meshtastic --set telemetry.environment_update_interval 300
```
