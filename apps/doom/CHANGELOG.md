# Changelog — Doom

## [0.1.0]
- Port de romalik/m5cardputer_doom (`d8de0d1`), compilado con ESP-IDF v4.4.6.
- Soporte para Cardputer-Adv: teclado por TCA8418 (I2C) y parlante por ES8311.
  Se detecta solo; en la Cardputer original sigue usando la matriz GPIO.
- Sin IWAD embebido: el WAD convertido se carga desde la microSD.
