// Cardputer-Adv + RA-02 (SX1278 / RFM98, 433 MHz) en el header EXT 2.54-14P.
// Basada en variants/esp32s3/m5stack_cardputer_adv del firmware upstream:
// display, teclado, codec e IMU iguales; solo cambia la radio.
//
// Cableado RA-02 -> EXT 14P
//   SCK  -> G40     MISO -> G39     MOSI -> G14
//   NSS  -> G5      RST  -> G3      DIO0 -> G4
//   3.3V -> 3V3 (nunca 5V)          GND  -> GND
//   DIO1..DIO5 sin conectar. Antena en el IPEX antes de transmitir.

#define USE_ST7789
#define HAS_SPI_TFT 1
#define ST7789_NSS 37
#define ST7789_RS 34  // DC
#define ST7789_SDA 35 // MOSI
#define ST7789_SCK 36
#define ST7789_RESET 33
#define ST7789_MISO -1
#define ST7789_BUSY -1
#define VTFT_LEDA 38
#define TFT_BACKLIGHT_ON HIGH
#define ST7789_SPI_HOST SPI2_HOST
#define SPI_FREQUENCY 40000000
#define SPI_READ_FREQUENCY 16000000
#define TFT_HEIGHT 135
#define TFT_WIDTH 240
#define TFT_OFFSET_X 0
#define TFT_OFFSET_Y 0
#define HAS_PHYSICAL_KEYBOARD 1

#define BUTTON_PIN 0

#define I2C_SDA 8
#define I2C_SCL 9

// Grove HY2.0-4P: queda libre para sensores I2C externos
#define I2C_SDA1 2
#define I2C_SCL1 1

// ---- Radio: RA-02 (SX1278) ----------------------------------------------
#undef LORA_SCK
#undef LORA_MISO
#undef LORA_MOSI
#undef LORA_CS

#define LORA_SCK 40
#define LORA_MISO 39
#define LORA_MOSI 14
#define LORA_CS 5 // NSS

#define USE_RF95
#define LORA_DIO0 4 // IRQ (RxDone/TxDone)
#define LORA_RESET 3
#define LORA_RST 3
#define LORA_DIO1 RADIOLIB_NC
#define LORA_DIO2 RADIOLIB_NC
#define LORA_DIO3 RADIOLIB_NC
// Sin SX126X_*, sin DIO2 como RF switch, sin TCXO: el SX1278 no los usa.

// ---- Sin GPS: el RA-02 ocupa el lugar del Cap y no trae GNSS ------------
#undef GPS_RX_PIN
#undef GPS_TX_PIN
#define HAS_GPS 0

// ---- Audio codec ES8311 --------------------------------------------------
#define HAS_I2S
#define DAC_I2S_BCK 41
#define DAC_I2S_WS 43
#define DAC_I2S_DOUT 42
#define DAC_I2S_DIN 46
#define DAC_I2S_MCLK 45 // dummy

// ---- Teclado TCA8418 -----------------------------------------------------
#define I2C_NO_RESCAN
#define KB_INT 11

#define HAS_NEOPIXEL
#define NEOPIXEL_COUNT 1
#define NEOPIXEL_DATA 21
#define NEOPIXEL_TYPE (NEO_GRB + NEO_KHZ800)

#define BATTERY_PIN 10
#define ADC_CHANNEL ADC_CHANNEL_9
#define ADC_MULTIPLIER 2 * 1.02

#define HAS_BMI270
