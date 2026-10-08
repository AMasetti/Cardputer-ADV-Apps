// MP3 Player para M5Stack Cardputer / Cardputer-Adv
//
// Lee los .mp3 de /music en la microSD y los reproduce por el parlante
// (o el jack en la Adv) usando ESP8266Audio + M5.Speaker.
//
// Teclas:
//   ;  .    subir / bajar en la lista
//   enter   reproducir el seleccionado
//   space   pausa / reanudar
//   ,  /    anterior / siguiente
//   -  =    volumen
//   s       shuffle on/off

#include <M5Cardputer.h>
#include <SD.h>
#include <SPI.h>
#include <vector>

#include <AudioFileSourceSD.h>
#include <AudioFileSourceBuffer.h>
#include <AudioFileSourceID3.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutput.h>

#ifndef APP_VERSION
#define APP_VERSION "dev"
#endif

// --- SD en el bus SPI de la Cardputer -------------------------------------
static constexpr int SD_SCK = 40;
static constexpr int SD_MISO = 39;
static constexpr int SD_MOSI = 14;
static constexpr int SD_CS = 12;
static const char *MUSIC_DIR = "/music";

// --- Salida de audio: puente ESP8266Audio -> M5.Speaker --------------------
class AudioOutputM5Speaker : public AudioOutput {
  public:
    explicit AudioOutputM5Speaker(m5::Speaker_Class *spk, uint8_t ch = 0) : _spk(spk), _ch(ch) {}

    bool begin() override { return true; }

    bool ConsumeSample(int16_t sample[2]) override
    {
        if (_idx < BUF_LEN) {
            _buf[_bank][_idx] = sample[0];
            _buf[_bank][_idx + 1] = sample[1];
            _idx += 2;
            return true;
        }
        flush();
        return false;
    }

    void flush() override
    {
        if (_idx) {
            _spk->playRaw(_buf[_bank], _idx, hertz, true, 1, _ch);
            _bank = (_bank + 1) % BANKS;
            _idx = 0;
        }
    }

    bool stop() override
    {
        flush();
        _spk->stop(_ch);
        return true;
    }

  private:
    static constexpr size_t BUF_LEN = 640;
    static constexpr size_t BANKS = 3;
    m5::Speaker_Class *_spk;
    uint8_t _ch;
    int16_t _buf[BANKS][BUF_LEN];
    size_t _bank = 0;
    size_t _idx = 0;
};

// --- Estado ---------------------------------------------------------------
static std::vector<String> tracks;
static int selected = 0;
static int playing = -1;
static bool paused = false;
static bool shuffle = false;
static uint8_t volume = 128;
static String tagTitle, tagArtist;

static AudioFileSourceSD *src = nullptr;
static AudioFileSourceBuffer *buff = nullptr;
static AudioFileSourceID3 *id3 = nullptr;
static AudioGeneratorMP3 *mp3 = nullptr;
static AudioOutputM5Speaker *out = nullptr;

static M5Canvas canvas(&M5Cardputer.Display);
static bool dirty = true;

// --- Biblioteca -----------------------------------------------------------
static bool isMp3(const String &name)
{
    String n = name;
    n.toLowerCase();
    return n.endsWith(".mp3") && !n.startsWith("._");
}

static void scanDir(const char *path, int depth)
{
    File dir = SD.open(path);
    if (!dir || !dir.isDirectory()) return;
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        String p = f.path();
        if (f.isDirectory()) {
            if (depth < 2) scanDir(p.c_str(), depth + 1);
        } else if (isMp3(f.name())) {
            tracks.push_back(p);
        }
        f.close();
    }
    dir.close();
}

static String baseName(const String &path)
{
    int slash = path.lastIndexOf('/');
    String n = path.substring(slash + 1);
    int dot = n.lastIndexOf('.');
    return dot > 0 ? n.substring(0, dot) : n;
}

// --- Reproducción ---------------------------------------------------------
static void id3Callback(void *, const char *type, bool isUnicode, const char *string)
{
    if (isUnicode) return; // la fuente por defecto no dibuja UTF-16
    if (!strcmp(type, "Title")) tagTitle = string;
    else if (!strcmp(type, "Artist") || !strcmp(type, "Performer")) tagArtist = string;
    dirty = true;
}

static void stopTrack()
{
    if (mp3 && mp3->isRunning()) mp3->stop();
    delete mp3; mp3 = nullptr;
    delete id3; id3 = nullptr;
    delete buff; buff = nullptr;
    delete src; src = nullptr;
    M5Cardputer.Speaker.stop();
}

static void playTrack(int idx)
{
    if (tracks.empty()) return;
    idx = (idx + tracks.size()) % tracks.size();
    stopTrack();
    tagTitle = "";
    tagArtist = "";

    src = new AudioFileSourceSD(tracks[idx].c_str());
    buff = new AudioFileSourceBuffer(src, 4096);
    id3 = new AudioFileSourceID3(buff);
    id3->RegisterMetadataCB(id3Callback, nullptr);
    mp3 = new AudioGeneratorMP3();
    if (!mp3->begin(id3, out)) {
        stopTrack();
        playing = -1;
    } else {
        playing = idx;
        selected = idx;
        paused = false;
    }
    dirty = true;
}

static int nextIndex(int step)
{
    if (tracks.empty()) return 0;
    if (shuffle && tracks.size() > 1) {
        int r;
        do { r = random(tracks.size()); } while (r == playing);
        return r;
    }
    return (playing < 0 ? selected : playing) + step;
}

// --- UI -------------------------------------------------------------------
static void draw()
{
    const int W = canvas.width(), H = canvas.height();
    canvas.fillSprite(TFT_BLACK);
    canvas.setTextSize(1);

    // Encabezado
    canvas.fillRect(0, 0, W, 14, 0x18E3);
    canvas.setTextColor(TFT_WHITE, 0x18E3);
    canvas.setCursor(4, 3);
    canvas.printf("MP3 %s", APP_VERSION);
    canvas.setCursor(W - 92, 3);
    canvas.printf("vol %3d %s", volume * 100 / 255, shuffle ? "SHF" : "   ");

    if (tracks.empty()) {
        canvas.setTextColor(TFT_ORANGE, TFT_BLACK);
        canvas.setCursor(4, 40);
        canvas.print("No hay .mp3 en /music");
        canvas.setCursor(4, 56);
        canvas.print("Copia tus temas a la SD.");
        canvas.pushSprite(0, 0);
        return;
    }

    // Lista
    const int rowH = 12, top = 18, rows = 7;
    int first = max(0, min(selected - rows / 2, (int)tracks.size() - rows));
    for (int i = 0; i < rows && first + i < (int)tracks.size(); ++i) {
        int t = first + i;
        int y = top + i * rowH;
        bool sel = t == selected;
        if (sel) canvas.fillRect(0, y - 1, W, rowH, 0x3186);
        uint16_t bg = sel ? 0x3186 : TFT_BLACK;
        canvas.setTextColor(t == playing ? TFT_GREENYELLOW : TFT_LIGHTGREY, bg);
        canvas.setCursor(4, y + 1);
        String n = baseName(tracks[t]);
        if (n.length() > 37) n = n.substring(0, 36) + "~";
        canvas.print(t == playing ? (paused ? "|| " : "> ") : "  ");
        canvas.print(n);
    }

    // Barra inferior: tema actual
    canvas.fillRect(0, H - 14, W, 14, 0x18E3);
    canvas.setTextColor(TFT_WHITE, 0x18E3);
    canvas.setCursor(4, H - 11);
    if (playing >= 0) {
        String now = tagTitle.length() ? tagTitle : baseName(tracks[playing]);
        if (tagArtist.length()) now = tagArtist + " - " + now;
        if (now.length() > 38) now = now.substring(0, 37) + "~";
        canvas.print(now);
    } else {
        canvas.printf("%d temas  enter:play", (int)tracks.size());
    }
    canvas.pushSprite(0, 0);
}

// --- Teclado --------------------------------------------------------------
static void handleKeys()
{
    if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) return;
    auto st = M5Cardputer.Keyboard.keysState();

    if (st.enter) playTrack(selected);
    if (st.space && playing >= 0) {
        paused = !paused;
        if (paused) M5Cardputer.Speaker.stop();
    }
    for (char c : st.word) {
        switch (c) {
        case ';': selected = max(0, selected - 1); break;
        case '.': selected = min((int)tracks.size() - 1, selected + 1); break;
        case ',': playTrack(nextIndex(-1)); break;
        case '/': playTrack(nextIndex(+1)); break;
        case '-': volume = volume > 16 ? volume - 16 : 0; M5Cardputer.Speaker.setVolume(volume); break;
        case '=': volume = volume < 239 ? volume + 16 : 255; M5Cardputer.Speaker.setVolume(volume); break;
        case 's': shuffle = !shuffle; break;
        }
    }
    dirty = true;
}

// --- Arduino --------------------------------------------------------------
void setup()
{
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setRotation(1);
    canvas.createSprite(M5Cardputer.Display.width(), M5Cardputer.Display.height());

    M5Cardputer.Speaker.begin();
    M5Cardputer.Speaker.setVolume(volume);
    out = new AudioOutputM5Speaker(&M5Cardputer.Speaker, 0);

    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    if (SD.begin(SD_CS, SPI, 25000000)) {
        scanDir(MUSIC_DIR, 0);
        std::sort(tracks.begin(), tracks.end());
    }
    randomSeed(esp_random());
    draw();
}

void loop()
{
    M5Cardputer.update();
    handleKeys();

    if (mp3 && !paused && mp3->isRunning()) {
        if (!mp3->loop()) {
            int next = nextIndex(+1);
            if (!shuffle && next >= (int)tracks.size()) {
                stopTrack();
                playing = -1;
                dirty = true;
            } else {
                playTrack(next);
            }
        }
    }

    static uint32_t lastDraw = 0;
    if (dirty && millis() - lastDraw > 60) {
        draw();
        dirty = false;
        lastDraw = millis();
    }
}
