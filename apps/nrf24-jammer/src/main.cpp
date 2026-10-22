// nRF24 Jammer para M5Stack Cardputer-Adv
#include <M5Cardputer.h>
#include <RF24.h>

#ifndef APP_VERSION
#define APP_VERSION "dev"
#endif

#define CE_PIN  3
#define CSN_PIN 5

RF24 radio(CE_PIN, CSN_PIN);

enum Menu { MENU_BT, MENU_DRONE, MENU_WIFI, MENU_BLE, MENU_ZIGBEE, MENU_MISC, MENU_COUNT };

struct State {
    int menu = 0;
    bool jamming = false;
    bool stop = false;
} st;

static const uint16_t BG    = lgfx::color565(10,14,22);
static const uint16_t PANEL = lgfx::color565(24,30,42);
static const uint16_t DIM   = lgfx::color565(90,100,120);
static const uint16_t TXT   = lgfx::color565(225,230,240);
static const uint16_t GREEN = lgfx::color565(60,230,120);
static const uint16_t RED   = lgfx::color565(255,80,70);
static const uint16_t ACC   = lgfx::color565(80,160,255);

const char* menuName(int m) {
    switch(m) {
        case 0: return "Bluetooth";
        case 1: return "Drones";
        case 2: return "WiFi";
        case 3: return "BLE";
        case 4: return "Zigbee";
        case 5: return "Misc";
        default: return "?";
    }
}

void drawUI() {
    auto& dsp = M5Cardputer.Display;
    dsp.fillScreen(BG);
    
    // Header
    dsp.fillRect(0, 0, 240, 16, PANEL);
    dsp.setTextFont(&fonts::lgfxJapanGothicP_12);
    dsp.setTextDatum(textdatum_t::middle_left);
    dsp.setTextColor(TXT);
    dsp.drawString("nRF24 Jammer", 4, 8);
    
    // Connection status
    bool connected = radio.isChipConnected();
    dsp.setTextDatum(textdatum_t::middle_right);
    dsp.setTextColor(connected ? GREEN : RED);
    dsp.drawString(connected ? "OK" : "N/C", 236, 8);
    
    if (st.jamming) {
        dsp.setFont(&fonts::DejaVu40);
        dsp.setTextDatum(textdatum_t::middle_center);
        dsp.setTextColor(RED);
        dsp.drawString("JAMMING", 120, 50);
        dsp.setFont(&fonts::DejaVu24);
        dsp.setTextColor(ACC);
        dsp.drawString(menuName(st.menu), 120, 90);
        dsp.setFont(&fonts::lgfxJapanGothicP_12);
        dsp.setTextColor(DIM);
        dsp.drawString("backspace para detener", 120, 120);
    } else {
        // Menu
        for (int i = 0; i < MENU_COUNT; i++) {
            int y = 28 + i * 16;
            bool sel = (i == st.menu);
            if (sel) {
                dsp.fillRect(4, y - 7, 232, 14, ACC);
                dsp.setTextColor(BG);
            } else {
                dsp.setTextColor(TXT);
            }
            dsp.setFont(&fonts::lgfxJapanGothicP_12);
            dsp.setTextDatum(textdatum_t::middle_left);
            dsp.drawString(menuName(i), 8, y);
        }
        dsp.setTextColor(DIM);
        dsp.setTextDatum(textdatum_t::middle_center);
        dsp.drawString("; . nav | enter jamming", 120, 128);
    }
}

void jamTask() {
    uint8_t buf[32];
    memset(buf, 0xAA, sizeof(buf));
    
    radio.setPALevel(RF24_PA_MAX);
    radio.setDataRate(RF24_2MBPS);
    radio.stopListening();
    
    st.jamming = true;
    drawUI();
    
    while (!st.stop) {
        for (uint8_t c = 0; c <= 80 && !st.stop; c++) {
            radio.setChannel(c);
            radio.write(buf, sizeof(buf));
        }
    }
    
    st.jamming = false;
    drawUI();
}

void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setBrightness(160);
    
    radio.begin();
    
    drawUI();
}

void loop() {
    M5Cardputer.update();
    
    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        auto stkeys = M5Cardputer.Keyboard.keysState();
        
        if (st.jamming) {
            if (stkeys.del) st.stop = true;
            return;
        }
        
        for (char c : stkeys.word) {
            if (c == ';') {
                if (st.menu > 0) st.menu--;
            } else if (c == '.') {
                if (st.menu < MENU_COUNT - 1) st.menu++;
            } else if (c == '\r' || c == '\n') {
                st.stop = false;
                jamTask();
            }
        }
        drawUI();
    }
    
    delay(20);
}
