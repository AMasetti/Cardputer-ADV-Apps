// nRF24 Jammer para M5Stack Cardputer-Adv
// Basado en FZ_nRF24_jammer de W0rthlessS0ul
//
// Navegacion:
//   ;  .         menu anterior / siguiente
//   ,  /         ajustar valor (-1 / +1)
//   <  >         ajustar valor (-10 / +10)
//   enter        seleccionar / iniciar jamming
//   space        cambiar modo
//   backspace    volver / detener jamming
//
// Pines nRF24L01+ (SPI) en el header EXT 2.54-14P:
//   CE  -> G3    CSN -> G5     SCK -> G40
//   MOSI -> G14  MISO -> G39   VCC -> 3V3   GND -> GND

#include <M5Cardputer.h>
#include <RF24.h>
#include <Preferences.h>

#ifndef APP_VERSION
#define APP_VERSION "dev"
#endif

#define NRF24_CE   3
#define NRF24_CSN  5

static const uint8_t BT_CHANS[]  = {32,34,46,48,50,52,0,1,2,4,6,8,22,24,26,28,30,74,76,78,80};
static const uint8_t BLE_CHANS[] = {2,26,80};
static const uint8_t ZB_CHANS[]  = {11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26};

RF24 radio(NRF24_CE, NRF24_CSN);
Preferences prefs;
M5Canvas spr(&M5Cardputer.Display);

enum Menu { MENU_BT, MENU_DRONE, MENU_WIFI, MENU_BLE, MENU_ZIGBEE, MENU_MISC, MENU_COUNT };
enum BtMode  { BT_LIST, BT_RAND, BT_BRUTE };
enum DrMode  { DR_BRUTE, DR_RAND };
enum WfMode  { WF_ALL, WF_SEL };
enum MsMode  { MS_CHAN, MS_PACK };
enum BlMode  { BL_ADV, BL_DATA };

struct State {
    Menu menu = MENU_BT;
    bool jamming = false;
    bool stop = false;
    bool connected = false;
    BtMode  bt  = BT_LIST;
    DrMode  dr  = DR_BRUTE;
    WfMode  wf  = WF_ALL;
    MsMode  ms  = MS_CHAN;
    BlMode  bl  = BL_ADV;
    uint8_t msStart = 0;
    uint8_t msStop  = 125;
    bool    msSetStart = true;
    uint8_t wfChan = 1;
    bool    subMenu = false;
} st;

static const uint16_t BG    = lgfx::color565(10,14,22);
static const uint16_t PANEL = lgfx::color565(24,30,42);
static const uint16_t DIM   = lgfx::color565(90,100,120);
static const uint16_t TXT   = lgfx::color565(225,230,240);
static const uint16_t GREEN = lgfx::color565(60,230,120);
static const uint16_t RED   = lgfx::color565(255,80,70);
static const uint16_t ACC   = lgfx::color565(80,160,255);
static const uint16_t YEL   = lgfx::color565(255,200,40);

static const char* mn(Menu m) {
    switch(m){case MENU_BT:return "Bluetooth";case MENU_DRONE:return "Drones";
    case MENU_WIFI:return "WiFi";case MENU_BLE:return "BLE";
    case MENU_ZIGBEE:return "Zigbee";case MENU_MISC:return "Misc";default:return"?";}
}
static const char* btn(BtMode m){switch(m){case BT_LIST:return"Lista";case BT_RAND:return"Random";default:return"Brute";}}
static const char* drn(DrMode m){switch(m){case DR_BRUTE:return"Brute";default:return"Random";}}
static const char* msn(MsMode m){switch(m){case MS_CHAN:return"Switch";default:return"Packets";}}
static const char* bln(BlMode m){switch(m){case BL_ADV:return"Adv";default:return"Data";}}

static void header() {
    spr.fillRect(0,0,240,16,PANEL);
    spr.setFont(&fonts::lgfxJapanGothicP_12);
    spr.setTextDatum(textdatum_t::middle_left);
    spr.setTextColor(TXT); spr.drawString("nRF24 Jammer",4,8);
    spr.setTextDatum(textdatum_t::middle_right);
    spr.setTextColor(st.connected?GREEN:RED);
    spr.drawString(st.connected?"OK":"N/C",236,8);
}

static void drawMain() {
    header();
    for(int i=0;i<MENU_COUNT;i++){
        int y=22+i*16;
        bool sel=(i==(int)st.menu);
        if(sel){spr.fillRect(4,y-6,232,14,ACC);spr.setTextColor(BG);}
        else spr.setTextColor(TXT);
        spr.setFont(&fonts::lgfxJapanGothicP_12);
        spr.setTextDatum(textdatum_t::middle_left);
        spr.drawString(mn((Menu)i),8,y);
    }
    spr.setTextColor(DIM); spr.setTextDatum(textdatum_t::middle_center);
    spr.drawString(";. nav | enter select",120,130);
}

static void drawJam() {
    spr.fillSprite(BG);
    spr.setFont(&fonts::DejaVu40); spr.setTextDatum(textdatum_t::middle_center);
    spr.setTextColor(RED); spr.drawString("JAMMING",120,50);
    spr.setFont(&fonts::DejaVu24); spr.setTextColor(YEL);
    spr.drawString(mn(st.menu),120,90);
    spr.setFont(&fonts::lgfxJapanGothicP_12); spr.setTextColor(DIM);
    spr.drawString("backspace para detener",120,120);
}

static void drawSub() {
    header();
    spr.setFont(&fonts::DejaVu24); spr.setTextDatum(textdatum_t::middle_center);
    spr.setTextColor(ACC); spr.drawString(mn(st.menu),120,28);

    spr.setFont(&fonts::lgfxJapanGothicP_14); spr.setTextColor(TXT);
    switch(st.menu){
    case MENU_BT:
        spr.drawString("Modo:",70,55);
        spr.setTextColor(YEL); spr.drawString(btn(st.bt),160,55);
        break;
    case MENU_DRONE:
        spr.drawString("Modo:",70,55);
        spr.setTextColor(YEL); spr.drawString(drn(st.dr),160,55);
        break;
    case MENU_WIFI:{
        spr.drawString("Modo:",70,55);
        spr.setTextColor(YEL); spr.drawString(st.wf==WF_ALL?"Todos":"Select",160,55);
        if(st.wf==WF_SEL && st.subMenu){
            char b[8]; snprintf(b,sizeof(b),"%d",st.wfChan);
            spr.setFont(&fonts::DejaVu56); spr.setTextColor(YEL);
            spr.drawString(b,120,85);
        }
        break;}
    case MENU_BLE:
        spr.drawString("Modo:",70,55);
        spr.setTextColor(YEL); spr.drawString(bln(st.bl),160,55);
        break;
    case MENU_ZIGBEE:
        spr.drawString("Canales 11-26",120,55);
        break;
    case MENU_MISC:{
        spr.drawString("Modo:",70,50);
        spr.setTextColor(YEL); spr.drawString(msn(st.ms),160,50);
        spr.setFont(&fonts::lgfxJapanGothicP_12); spr.setTextColor(TXT);
        spr.drawString("Start:",60,72);
        spr.setTextColor(st.msSetStart?ACC:TXT);
        spr.drawString(String(st.msStart).c_str(),150,72);
        spr.setTextColor(TXT); spr.drawString("Stop:",60,90);
        spr.setTextColor(!st.msSetStart?ACC:TXT);
        spr.drawString(String(st.msStop).c_str(),150,90);
        break;}
    default:break;
    }
    spr.setFont(&fonts::lgfxJapanGothicP_12); spr.setTextColor(DIM);
    spr.setTextDatum(textdatum_t::middle_center);
    if(st.menu==MENU_MISC)
        spr.drawString("</> ajustar | enter cambiar",120,118);
    else if(st.menu==MENU_WIFI && st.wf==WF_SEL && st.subMenu)
        spr.drawString("</> ajustar | enter start",120,118);
    else
        spr.drawString("enter start | back volver",120,118);
}

static void render(){
    spr.fillSprite(BG);
    if(st.jamming) drawJam();
    else if(st.subMenu) drawSub();
    else drawMain();
    spr.pushSprite(0,0);
}

static void jamTask(){
    uint8_t buf[32]; memset(buf,0xAA,sizeof(buf));
    radio.setPALevel(RF24_PA_MAX);
    radio.setDataRate(RF24_2MBPS);
    radio.stopListening();

    if(st.menu==MENU_BT){
        while(!st.stop){
            if(st.bt==BT_LIST) for(size_t i=0;i<sizeof(BT_CHANS)&&!st.stop;i++){radio.setChannel(BT_CHANS[i]);radio.write(buf,sizeof(buf));}
            else if(st.bt==BT_RAND) {radio.setChannel(random(0,81));radio.write(buf,sizeof(buf));}
            else for(uint8_t c=0;c<=80&&!st.stop;c++){radio.setChannel(c);radio.write(buf,sizeof(buf));}
        }
    } else if(st.menu==MENU_DRONE){
        while(!st.stop){
            if(st.dr==DR_RAND){radio.setChannel(random(0,126));radio.write(buf,sizeof(buf));}
            else for(uint8_t c=0;c<=125&&!st.stop;c++){radio.setChannel(c);radio.write(buf,sizeof(buf));}
        }
    } else if(st.menu==MENU_WIFI){
        while(!st.stop){
            if(st.wf==WF_ALL){
                for(uint8_t ch=0;ch<=13&&!st.stop;ch++)
                    for(uint8_t c=ch*5+1;c<=ch*5+23&&!st.stop;c++){radio.setChannel(c);radio.write(buf,sizeof(buf));}
            } else {
                uint8_t ch=st.wfChan-1;
                for(uint8_t c=ch*5+1;c<=ch*5+23&&!st.stop;c++){radio.setChannel(c);radio.write(buf,sizeof(buf));}
            }
        }
    } else if(st.menu==MENU_BLE){
        while(!st.stop){
            if(st.bl==BL_ADV) for(size_t i=0;i<sizeof(BLE_CHANS)&&!st.stop;i++){radio.setChannel(BLE_CHANS[i]);radio.write(buf,sizeof(buf));}
            else for(uint8_t c=2;c<=80&&!st.stop;c+=2){radio.setChannel(c);radio.write(buf,sizeof(buf));}
        }
    } else if(st.menu==MENU_ZIGBEE){
        while(!st.stop){
            for(size_t i=0;i<sizeof(ZB_CHANS)&&!st.stop;i++){
                uint8_t s=4+5*(ZB_CHANS[i]-11);
                for(uint8_t c=s;c<=s+2&&!st.stop;c++){radio.setChannel(c);radio.write(buf,sizeof(buf));}
            }
        }
    } else if(st.menu==MENU_MISC){
        while(!st.stop){
            for(uint8_t c=st.msStart;c<=st.msStop&&!st.stop;c++){radio.setChannel(c);radio.write(buf,sizeof(buf));}
        }
    }
}

static void startJam(){if(!st.connected)return;st.jamming=true;st.stop=false;render();jamTask();st.jamming=false;render();}
static void stopJam(){st.stop=true;st.jamming=false;}

static void keys(){
    if(!M5Cardputer.Keyboard.isChange()||!M5Cardputer.Keyboard.isPressed())return;
    auto k=M5Cardputer.Keyboard.keysState();
    if(st.jamming){if(k.del)stopJam();return;}

    if(k.keys.find(';')!=std::string::npos){
        if(!st.subMenu&&st.menu>0)st.menu=(Menu)((int)st.menu-1);
        render(); return;
    }
    if(k.keys.find('.')!=std::string::npos){
        if(!st.subMenu&&st.menu<MENU_COUNT-1)st.menu=(Menu)((int)st.menu+1);
        render(); return;
    }
    if(k.del){
        if(st.subMenu){st.subMenu=false;st.msSetStart=true;}
        render(); return;
    }
    if(k.enter){
        if(st.subMenu){
            if(st.menu==MENU_MISC){
                if(st.msSetStart){st.msSetStart=false;render();return;}
            }
            startJam(); return;
        }
        st.subMenu=true; render(); return;
    }

    for(char c:k.word){
        switch(c){
        case ',': case '<':
            if(st.menu==MENU_MISC&&st.subMenu){
                if(st.msSetStart){if(st.msStart>0)st.msStart--;}
                else{if(st.msStop>st.msStart)st.msStop--;}
            } else if(st.menu==MENU_WIFI&&st.subMenu&&st.wf==WF_SEL){if(st.wfChan>1)st.wfChan--;}
            break;
        case '/': case '>':
            if(st.menu==MENU_MISC&&st.subMenu){
                if(st.msSetStart){if(st.msStart<124)st.msStart++;}
                else{if(st.msStop<125)st.msStop++;}
            } else if(st.menu==MENU_WIFI&&st.subMenu&&st.wf==WF_SEL){if(st.wfChan<14)st.wfChan++;}
            break;
        case ' ': case 'm': case 'M':
            if(st.menu==MENU_BT&&st.subMenu)st.bt=(BtMode)(((int)st.bt+1)%3);
            else if(st.menu==MENU_DRONE&&st.subMenu)st.dr=(DrMode)(((int)st.dr+1)%2);
            else if(st.menu==MENU_WIFI&&st.subMenu)st.wf=(WfMode)(((int)st.wf+1)%2);
            else if(st.menu==MENU_BLE&&st.subMenu)st.bl=(BlMode)(((int)st.bl+1)%2);
            else if(st.menu==MENU_MISC&&st.subMenu)st.ms=(MsMode)(((int)st.ms+1)%2);
            break;
        }
    }
    render();
}

void setup(){
    auto cfg=M5.config();
    M5Cardputer.begin(cfg,true);
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setBrightness(160);
    if(radio.begin()){st.connected=true;radio.setAutoAck(false);}
    prefs.begin("nrf24jam",false);
    st.bt=(BtMode)prefs.getUChar("bt",0);
    st.dr=(DrMode)prefs.getUChar("dr",0);
    spr.setColorDepth(16); spr.createSprite(240,135);
    spr.fillSprite(BG);
    spr.setFont(&fonts::DejaVu40); spr.setTextDatum(textdatum_t::middle_center);
    spr.setTextColor(ACC); spr.drawString("nRF24 Jammer",120,55);
    spr.setFont(&fonts::lgfxJapanGothicP_12); spr.setTextColor(DIM);
    spr.drawString("v" APP_VERSION,120,92);
    spr.pushSprite(0,0);
    delay(800); render();
}

void loop(){
    M5Cardputer.update();
    keys();
    delay(20);
}
