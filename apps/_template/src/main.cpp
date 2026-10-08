#include <M5Cardputer.h>

#ifndef APP_VERSION
#define APP_VERSION "dev"
#endif

void setup()
{
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.drawString("__APP__", 8, 8);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.drawString("v" APP_VERSION, 8, 32);
}

void loop()
{
    M5Cardputer.update();
    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        auto st = M5Cardputer.Keyboard.keysState();
        String s;
        for (char c : st.word) s += c;
        M5Cardputer.Display.fillRect(8, 60, 220, 16, TFT_BLACK);
        M5Cardputer.Display.drawString(s, 8, 60);
    }
}
