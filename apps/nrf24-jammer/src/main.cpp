// nRF24 Jammer para M5Stack Cardputer-Adv
#include <M5Cardputer.h>
#include <RF24.h>

#ifndef APP_VERSION
#define APP_VERSION "dev"
#endif

RF24 radio(3, 5);

int menu = 0;
bool jamming = false;
const char* menus[] = {"Bluetooth", "Drones", "WiFi", "BLE", "Zigbee", "Misc"};

void draw() {
    auto& d = M5Cardputer.Display;
    d.fillScreen(0x0000);
    d.setTextColor(0xFFFF);
    d.setTextDatum(top_left);
    d.drawString("nRF24 Jammer", 4, 4);
    
    for (int i = 0; i < 6; i++) {
        if (i == menu) d.fillRect(4, 20 + i * 16, 232, 14, 0x001F);
        d.setTextColor(i == menu ? 0x0000 : 0xFFFF);
        d.drawString(menus[i], 8, 22 + i * 16);
    }
    
    if (jamming) {
        d.setTextColor(0xF800);
        d.setTextDatum(middle_center);
        d.drawString("JAMMING!", 120, 100);
    }
}

void jam() {
    uint8_t buf[32];
    memset(buf, 0xAA, 32);
    radio.setPALevel(RF24_PA_MAX);
    radio.setDataRate(RF24_2MBPS);
    radio.stopListening();
    
    jamming = true;
    draw();
    
    while (jamming) {
        for (int c = 0; c <= 80 && jamming; c++) {
            radio.setChannel(c);
            radio.write(buf, 32);
        }
        M5Cardputer.update();
        if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
            auto k = M5Cardputer.Keyboard.keysState();
            if (k.del) jamming = false;
        }
    }
    
    draw();
}

void setup() {
    M5Cardputer.begin(M5.config(), true);
    M5Cardputer.Display.setRotation(1);
    radio.begin();
    draw();
}

void loop() {
    M5Cardputer.update();
    
    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        auto k = M5Cardputer.Keyboard.keysState();
        
        if (k.del) {
            jamming = false;
        } else if (k.enter) {
            jam();
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
