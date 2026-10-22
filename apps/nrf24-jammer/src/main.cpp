// nRF24 Jammer para M5Stack Cardputer-Adv
// Basado en FZ_nRF24_jammer de W0rthlessS0ul
//
// Navegacion:
//   ;  .         menu anterior / siguiente
//   enter        seleccionar / iniciar jamming
//   backspace    volver / detener jamming
//
// Hardware:
//   nRF24L01+ VCC -> 3.3V (via AMS1117-3.3V desde 5V)
//   nRF24L01+ GND -> GND
//   nRF24L01+ CE  -> G3
//   nRF24L01+ CSN -> G5
//   nRF24L01+ SCK -> G40
//   nRF24L01+ MOSI -> G14
//   nRF24L01+ MISO -> G39

#include <M5Cardputer.h>
#include <RF24.h>
#include <SPI.h>

#ifndef APP_VERSION
#define APP_VERSION "dev"
#endif

// Pines nRF24
#define CE_PIN  3
#define CSN_PIN 5

RF24 radio(CE_PIN, CSN_PIN);

// Canales predefinidos
static const uint8_t BT_CHANS[]  = {32,34,46,48,50,52,0,1,2,4,6,8,22,24,26,28,30,74,76,78,80};
static const uint8_t BLE_CHANS[] = {2,26,80};
static const uint8_t ZB_CHANS[]  = {11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26};

// Estado
int menu = 0;
bool jamming = false;
bool module_ok = false;
const char* menus[] = {"Bluetooth", "Drones", "WiFi", "BLE", "Zigbee", "Misc"};

// Colores
const uint16_t C_BG    = 0x1082;  // fondo oscuro
const uint16_t C_PANEL = 0x2124;  // panel
const uint16_t C_TEXT  = 0xDEFB;  // texto claro
const uint16_t C_ACC   = 0x051F;  // azul acento
const uint16_t C_RED   = 0xF800;  // rojo
const uint16_t C_GREEN = 0x07E0;  // verde
const uint16_t C_YELLOW = 0xFFE0; // amarillo

void draw() {
    auto& d = M5Cardputer.Display;
    d.fillScreen(C_BG);
    
    // Header
    d.fillRect(0, 0, 240, 16, C_PANEL);
    d.setTextFont(&fonts::lgfxJapanGothicP_12);
    d.setTextDatum(textdatum_t::middle_left);
    d.setTextColor(C_TEXT);
    d.drawString("nRF24 Jammer", 4, 8);
    
    // Status
    d.setTextDatum(textdatum_t::middle_right);
    if (module_ok) {
        d.setTextColor(C_GREEN);
        d.drawString("OK", 236, 8);
    } else {
        d.setTextColor(C_RED);
        d.drawString("N/C", 236, 8);
    }
    
    if (jamming) {
        // Pantalla de jamming
        d.setFont(&fonts::DejaVu40);
        d.setTextDatum(textdatum_t::middle_center);
        d.setTextColor(C_RED);
        d.drawString("JAMMING", 120, 50);
        d.setFont(&fonts::DejaVu24);
        d.setTextColor(C_YELLOW);
        d.drawString(menus[menu], 120, 90);
        d.setFont(&fonts::lgfxJapanGothicP_12);
        d.setTextColor(C_TEXT);
        d.drawString("backspace para detener", 120, 120);
    } else {
        // Menu
        for (int i = 0; i < 6; i++) {
            int y = 28 + i * 16;
            bool sel = (i == menu);
            if (sel) {
                d.fillRect(4, y - 7, 232, 14, C_ACC);
                d.setTextColor(C_BG);
            } else {
                d.setTextColor(C_TEXT);
            }
            d.setFont(&fonts::lgfxJapanGothicP_12);
            d.setTextDatum(textdatum_t::middle_left);
            d.drawString(menus[i], 8, y);
        }
        
        // Footer
        d.setTextColor(0x8410);
        d.setTextDatum(textdatum_t::middle_center);
        d.drawString(";. nav | enter jamming", 120, 128);
    }
}

void jamBluetooth() {
    uint8_t buf[32];
    memset(buf, 0xAA, sizeof(buf));
    
    while (jamming) {
        for (size_t i = 0; i < sizeof(BT_CHANS) && jamming; i++) {
            radio.setChannel(BT_CHANS[i]);
            radio.write(buf, sizeof(buf));
        }
        // Check stop
        M5Cardputer.update();
        if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
            auto k = M5Cardputer.Keyboard.keysState();
            if (k.del) jamming = false;
        }
    }
}

void jamDrones() {
    uint8_t buf[32];
    memset(buf, 0xAA, sizeof(buf));
    
    while (jamming) {
        for (int c = 0; c <= 125 && jamming; c++) {
            radio.setChannel(c);
            radio.write(buf, sizeof(buf));
        }
        // Check stop
        M5Cardputer.update();
        if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
            auto k = M5Cardputer.Keyboard.keysState();
            if (k.del) jamming = false;
        }
    }
}

void jamWiFi() {
    uint8_t buf[32];
    memset(buf, 0xAA, sizeof(buf));
    
    while (jamming) {
        // WiFi usa canales 1-14 (frecuencias en 2.4 GHz)
        for (int ch = 0; ch <= 13 && jamming; ch++) {
            for (int c = ch * 5 + 1; c <= ch * 5 + 23 && jamming; c++) {
                radio.setChannel(c);
                radio.write(buf, sizeof(buf));
            }
        }
        // Check stop
        M5Cardputer.update();
        if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
            auto k = M5Cardputer.Keyboard.keysState();
            if (k.del) jamming = false;
        }
    }
}

void jamBLE() {
    uint8_t buf[32];
    memset(buf, 0xAA, sizeof(buf));
    
    while (jamming) {
        for (size_t i = 0; i < sizeof(BLE_CHANS) && jamming; i++) {
            radio.setChannel(BLE_CHANS[i]);
            radio.write(buf, sizeof(buf));
        }
        // Check stop
        M5Cardputer.update();
        if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
            auto k = M5Cardputer.Keyboard.keysState();
            if (k.del) jamming = false;
        }
    }
}

void jamZigbee() {
    uint8_t buf[32];
    memset(buf, 0xAA, sizeof(buf));
    
    while (jamming) {
        for (size_t i = 0; i < sizeof(ZB_CHANS) && jamming; i++) {
            uint8_t start = 4 + 5 * (ZB_CHANS[i] - 11);
            for (uint8_t c = start; c <= start + 2 && jamming; c++) {
                radio.setChannel(c);
                radio.write(buf, sizeof(buf));
            }
        }
        // Check stop
        M5Cardputer.update();
        if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
            auto k = M5Cardputer.Keyboard.keysState();
            if (k.del) jamming = false;
        }
    }
}

void jamMisc() {
    uint8_t buf[32];
    memset(buf, 0xAA, sizeof(buf));
    
    while (jamming) {
        for (int c = 0; c <= 125 && jamming; c++) {
            radio.setChannel(c);
            radio.write(buf, sizeof(buf));
        }
        // Check stop
        M5Cardputer.update();
        if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
            auto k = M5Cardputer.Keyboard.keysState();
            if (k.del) jamming = false;
        }
    }
}

void startJamming() {
    if (!module_ok) return;
    
    radio.setPALevel(RF24_PA_MAX);
    radio.setDataRate(RF24_2MBPS);
    radio.stopListening();
    
    jamming = true;
    draw();
    
    switch (menu) {
        case 0: jamBluetooth(); break;
        case 1: jamDrones(); break;
        case 2: jamWiFi(); break;
        case 3: jamBLE(); break;
        case 4: jamZigbee(); break;
        case 5: jamMisc(); break;
    }
    
    jamming = false;
    draw();
}

void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setRotation(1);
    
    // Inicializar SPI
    SPI.begin(40, 39, 14, 5);
    
    // Inicializar nRF24
    if (radio.begin()) {
        module_ok = true;
        radio.setAutoAck(false);
    }
    
    draw();
}

void loop() {
    M5Cardputer.update();
    
    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        auto k = M5Cardputer.Keyboard.keysState();
        
        if (k.del) {
            jamming = false;
        } else if (k.enter) {
            startJamming();
        } else {
            for (size_t i = 0; i < k.word.length(); i++) {
                char c = k.word[i];
                if (c == ';' && menu > 0) menu--;
                else if (c == '.' && menu < 5) menu++;
            }
        }
        draw();
    }
    
    delay(20);
}
