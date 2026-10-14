// BLE Sentinel: detector pasivo de dispositivos BLE por firma de
// manufacturer data. Ver docs/BLE_SENTINEL_SPEC.md para el diseño completo.
//
// v1 incluye: fingerprint resistente a rotación de MAC, SignatureDB con
// smart glasses (Meta/Luxottica, Snap), alertas de audio por categoría,
// log de avistamientos a SD (timestamp relativo al boot, sin RTC todavía),
// y ConvoyAnalyzer (detección de seguimiento sostenido entre "contextos"
// marcados a mano -- sin GPS no hay forma automática de saber que uno se
// movió). Pendiente para próximas versiones: RoomSweepEngine, WiFiSniffer,
// ExportModule, soporte de RTC por Grove.
//
// Controles:
//   G0 (mantenido ~0.8s)  activar/silenciar alertas de audio (arranca en mute)
//   espacio               pausar/reanudar el scan
//   c                     marcar "cambié de lugar" (nuevo contexto, ver ConvoyAnalyzer)
//   +/-                   volumen master
#include <M5Cardputer.h>
#include <SD.h>
#include <SPI.h>
#include <vector>
#include <algorithm>

#include "fingerprint.h"
#include "signature_db.h"
#include "sighting_store.h"
#include "ble_scanner.h"
#include "alert_engine.h"
#include "convoy_analyzer.h"

#ifndef APP_VERSION
#define APP_VERSION "dev"
#endif

namespace {

constexpr int SD_SCK = 40;
constexpr int SD_MISO = 39;
constexpr int SD_MOSI = 14;
constexpr int SD_CS = 12;

constexpr int SCREEN_W = 240;
constexpr int SCREEN_H = 135;
constexpr int HUD_H = 15;
constexpr int STATUS_H = 13;
constexpr int ROW_H = 12;
constexpr int LIST_TOP = HUD_H + STATUS_H;
constexpr int MAX_VISIBLE_ROWS = (SCREEN_H - LIST_TOP) / ROW_H;

M5Canvas canvas(&M5Cardputer.Display);
bool dirty = true;
bool sdOk = false;

// Paleta compartida con el resto del repo (shell-rc, led-rc): fondo casi
// negro, barra de HUD gris oscuro, acento rojo porque esto es una app de
// seguridad (alarma), verde/gris para "todo tranquilo".
uint16_t C_BG, C_HUD, C_TEXT, C_DIM, C_ACCENT, C_ON, C_OFF, C_WARN;

const char *categoryLabel(signaturedb::Category c)
{
    switch (c) {
        case signaturedb::Category::SmartGlasses: return "GLASSES";
        case signaturedb::Category::DroneRemoteId: return "DRONE";
        case signaturedb::Category::Headset: return "headset";
        default: return "unknown";
    }
}

uint16_t categoryColor(signaturedb::Category c, bool confidenceLow)
{
    switch (c) {
        case signaturedb::Category::SmartGlasses:
        case signaturedb::Category::DroneRemoteId:
            return confidenceLow ? C_WARN : C_ACCENT;
        case signaturedb::Category::Headset:
            return C_DIM;
        default:
            return C_ACCENT; // sin match -> tratado como sospechoso, ver signature_db.h
    }
}

// Resuelve categoría + perfil de alerta para un RawSighting ya mapeado a
// fingerprint. Único punto donde se decide "qué es esto", todo lo demás
// (UI, alertas, store) consume el resultado.
struct Resolved {
    signaturedb::Category category;
    signaturedb::AlertProfile alertProfile;
    bool confidenceLow;
};

Resolved resolve(const blescanner::RawSighting &raw, bool fpConfidenceLow)
{
    if (const signaturedb::Entry *e = signaturedb::matchByCompanyId(raw.companyId)) {
        return {e->category, e->alertProfile, fpConfidenceLow || !e->verified};
    }
    if (signaturedb::matchHeadsetByName(raw.localName)) {
        return {signaturedb::Category::Headset, signaturedb::AlertProfile::HeadsetLowPriority, fpConfidenceLow};
    }
    // Sin match: postura conservadora, se trata como sospechoso por defecto
    // (ver signature_db.h / AlertEngine).
    return {signaturedb::Category::Unknown, signaturedb::AlertProfile::SuspiciousDefault, fpConfidenceLow};
}

bool initSdCard()
{
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    // Degradado explícito si falla: seguimos funcionando, solo que sin log
    // persistente ni session_id continuo (ver sighting_store.h).
    return SD.begin(SD_CS, SPI, 25000000);
}

void initColors()
{
    C_BG = canvas.color565(10, 10, 14);
    C_HUD = canvas.color565(32, 28, 30);
    C_TEXT = canvas.color565(225, 225, 230);
    C_DIM = canvas.color565(120, 120, 130);
    C_ACCENT = canvas.color565(221, 29, 33);   // rojo: esto es una alarma
    C_ON = canvas.color565(80, 210, 110);      // verde: audio activo / ok
    C_OFF = canvas.color565(50, 50, 58);
    C_WARN = canvas.color565(230, 160, 40);    // ámbar: firma sin verificar
}

// --- Dibujo ------------------------------------------------------------------------

void drawHud()
{
    canvas.fillRect(0, 0, SCREEN_W, HUD_H, C_HUD);
    canvas.setFont(&fonts::Font0);
    canvas.setTextSize(1);
    canvas.setTextColor(C_ACCENT, C_HUD);
    canvas.setTextDatum(middle_left);
    canvas.drawString("SENTINEL", 4, HUD_H / 2);
    canvas.setTextColor(C_TEXT, C_HUD);
    canvas.setTextDatum(middle_center);
    canvas.drawString(blescanner::scanning() ? "escaneando" : "detenido", SCREEN_W / 2, HUD_H / 2);
    canvas.setTextColor(C_DIM, C_HUD);
    canvas.setTextDatum(middle_right);
    canvas.drawString("v" APP_VERSION, SCREEN_W - 4, HUD_H / 2);
}

// Chip tipo "pill" igual al de shell-rc/led-rc: rótulo con fondo lleno
// cuando está "on", borde nomás cuando está "off".
void drawChip(int x, int y, int w, const char *label, bool on, uint16_t onColor)
{
    canvas.fillRoundRect(x, y, w, 11, 3, on ? onColor : C_BG);
    if (!on) canvas.drawRoundRect(x, y, w, 11, 3, C_DIM);
    canvas.setTextDatum(middle_center);
    canvas.setFont(&fonts::Font0);
    canvas.setTextColor(on ? C_BG : C_DIM, on ? onColor : C_BG);
    canvas.drawString(label, x + w / 2, y + 6);
}

void drawStatusBar()
{
    canvas.fillRect(0, HUD_H, SCREEN_W, STATUS_H, C_BG);
    int y = HUD_H + 1;
    drawChip(4, y, 46, alertengine::enabled() ? "AUDIO ON" : "MUTE", alertengine::enabled(), C_ON);
    drawChip(54, y, 36, sdOk ? "SD OK" : "SD --", sdOk, C_ON);

    canvas.setTextDatum(middle_left);
    canvas.setTextColor(C_DIM, C_BG);
    canvas.drawString(String("ctx ") + convoyanalyzer::currentContext(), 94, y + 6);

    canvas.setTextDatum(middle_right);
    canvas.setTextColor(C_DIM, C_BG);
    canvas.drawString(String("vol ") + (alertengine::masterVolume() * 100 / 255) + "%",
                       SCREEN_W - 44, y + 6);

    char ses[12];
    if (sightingstore::sdAvailable()) {
        snprintf(ses, sizeof(ses), "#%lu", static_cast<unsigned long>(sightingstore::sessionId()));
    } else {
        snprintf(ses, sizeof(ses), "--");
    }
    canvas.drawString(ses, SCREEN_W - 4, y + 6);
}

// Barras de señal, mismo criterio que shell-rc: -90dBm (nada) a -50dBm (llena).
void drawSignalBars(int x, int y, int rssi, uint16_t color)
{
    int bars = constrain((rssi + 90) / 10, 0, 4);
    for (int b = 0; b < 4; b++) {
        int h = 2 + b * 2;
        canvas.fillRect(x + b * 4, y + 9 - h, 3, h, b < bars ? color : C_OFF);
    }
}

void drawList()
{
    canvas.fillRect(0, LIST_TOP, SCREEN_W, SCREEN_H - LIST_TOP, C_BG);

    size_t n = sightingstore::count();

    if (n == 0) {
        canvas.setFont(&fonts::Font0);
        canvas.setTextDatum(middle_center);
        canvas.setTextColor(C_DIM, C_BG);
        canvas.drawString(blescanner::scanning() ? "Escaneando..." : "Scan detenido",
                           SCREEN_W / 2, LIST_TOP + (SCREEN_H - LIST_TOP) / 2);
        return;
    }

    // Copiamos índices y ordenamos: primero los sospechosos de convoy
    // (posible seguimiento, la alerta más seria que tenemos), después por
    // RSSI desc dentro de cada grupo. Sin tocar el store, es solo para esta
    // pasada de dibujo.
    std::vector<size_t> order(n);
    for (size_t i = 0; i < n; i++) order[i] = i;
    std::sort(order.begin(), order.end(), [](size_t a, size_t b) {
        const sightingstore::Record *ra = sightingstore::byIndex(a);
        const sightingstore::Record *rb = sightingstore::byIndex(b);
        bool convoyA = convoyanalyzer::isSuspect(ra->fingerprintHash);
        bool convoyB = convoyanalyzer::isSuspect(rb->fingerprintHash);
        if (convoyA != convoyB) return convoyA;
        return ra->lastRssi > rb->lastRssi;
    });

    canvas.setFont(&fonts::Font0);
    size_t shown = std::min<size_t>(order.size(), MAX_VISIBLE_ROWS);
    for (size_t row = 0; row < shown; row++) {
        const sightingstore::Record *r = sightingstore::byIndex(order[row]);
        if (!r) continue;
        int y = LIST_TOP + row * ROW_H;
        bool convoySuspect = convoyanalyzer::isSuspect(r->fingerprintHash);
        uint16_t color = categoryColor(r->category, r->confidenceLow);

        // Franja de color a la izquierda: distingue la fila de un vistazo
        // sin tener que leer el label, igual que la cruz de shell-rc. Un
        // sospechoso de convoy pisa el color de categoría: es la alerta
        // más seria, no "este tipo de dispositivo".
        canvas.fillRect(0, y, 3, ROW_H - 1, convoySuspect ? C_ACCENT : color);

        canvas.setTextDatum(middle_left);
        canvas.setTextColor(convoySuspect ? C_ACCENT : color, C_BG);
        canvas.drawString(convoySuspect ? "!SIGUE" : categoryLabel(r->category), 6, y + ROW_H / 2);

        canvas.setTextColor(C_DIM, C_BG);
        canvas.setTextDatum(middle_right);
        canvas.drawString(r->lastMac.substring(9), 148, y + ROW_H / 2); // últimos 3 octetos
        if (r->confidenceLow) canvas.drawString("?", 156, y + ROW_H / 2);

        canvas.setTextColor(r->timesSeen > 1 ? C_TEXT : C_DIM, C_BG);
        canvas.drawString(String("x") + r->timesSeen, 184, y + ROW_H / 2);

        canvas.setTextDatum(middle_right);
        canvas.setTextColor(C_DIM, C_BG);
        canvas.drawString(String(r->lastRssi), SCREEN_W - 24, y + ROW_H / 2);
        drawSignalBars(SCREEN_W - 18, y + 1, r->lastRssi, color);
    }
}

void drawFooter(const char *text)
{
    canvas.setFont(&fonts::Font0);
    canvas.setTextDatum(bottom_center);
    canvas.setTextColor(C_DIM, C_BG);
    canvas.drawString(text, SCREEN_W / 2, SCREEN_H - 2);
}

// En una zona con varios dispositivos, cada paquete de advertising (cada
// ~100-1000ms por dispositivo) marca `dirty`. Sin throttle, la lista se
// reordena por RSSI en cada uno de esos paquetes y el resultado es un
// parpadeo/"scroll" constante. Redibujamos como máximo a este ritmo; el
// flag `dirty` sigue en pie entre medio, así que no se pierde ningún
// cambio, solo se agrupan.
constexpr uint32_t RENDER_INTERVAL_MS = 300;

void render()
{
    static uint32_t lastRenderMs = 0;
    uint32_t now = millis();
    if (!dirty || (now - lastRenderMs < RENDER_INTERVAL_MS)) return;
    lastRenderMs = now;

    canvas.fillScreen(C_BG);
    drawHud();
    drawStatusBar();
    drawList();
    drawFooter("c: lugar   space: scan   +/- vol   G0: audio");
    canvas.pushSprite(0, 0);
    dirty = false;
}

// --- Entrada -----------------------------------------------------------------------

void handleKeyboard()
{
    if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) return;
    auto st = M5Cardputer.Keyboard.keysState();
    for (char c : st.word) {
        if (c == '+') {
            alertengine::setMasterVolume(std::min(255, alertengine::masterVolume() + 20));
            dirty = true;
        } else if (c == '-') {
            alertengine::setMasterVolume(std::max(0, alertengine::masterVolume() - 20));
            dirty = true;
        } else if (c == ' ') {
            if (blescanner::scanning()) blescanner::stop();
            else blescanner::start();
            dirty = true;
        } else if (c == 'c') {
            convoyanalyzer::markNewContext();
            dirty = true;
        }
    }
}

constexpr uint32_t G0_LONG_PRESS_MS = 800;

// G0 (BtnA) mantenido activa/desactiva el audio. Arranca en mute (ver
// alert_engine.cpp) -- esto es la única forma de prenderlo, no hay tecla.
// `handled` evita que se togglee varias veces mientras el botón sigue
// apretado: un solo toggle por mantenido, igual que el patrón de `karting`.
void handleG0()
{
    static bool handled = false;
    if (M5Cardputer.BtnA.isPressed()) {
        if (!handled && M5Cardputer.BtnA.pressedFor(G0_LONG_PRESS_MS)) {
            alertengine::setEnabled(!alertengine::enabled());
            handled = true;
            dirty = true;
        }
    } else {
        handled = false;
    }
}

void processSightings()
{
    static std::vector<blescanner::RawSighting> batch;
    batch.clear();
    if (blescanner::poll(batch) == 0) return;

    for (const auto &raw : batch) {
        fingerprint::AdvInput fin;
        fin.companyId = raw.companyId;
        fin.payloadLen = raw.payloadLen;
        fin.txPowerDbm = raw.txPowerDbm;
        fin.serviceUuids16 = raw.serviceUuids16;
        // adTypesPresentMask queda en 0 en v1: distinguir qué AD types trae
        // (más allá de companyId/serviceUuids, que ya entran al fingerprint
        // por su cuenta) es una mejora de precisión para una v2, no bloquea
        // el resto del pipeline.
        fingerprint::Result fp = fingerprint::compute(fin);

        Resolved res = resolve(raw, fp.confidenceLow);

        sightingstore::Record rec;
        sightingstore::SightingKind kind = sightingstore::record(
            fp.hash, raw.mac, raw.rssi, res.category, res.confidenceLow, rec);

        if (kind == sightingstore::SightingKind::New ||
            kind == sightingstore::SightingKind::MacRotated) {
            alertengine::trigger(res.alertProfile, fp.hash);
        }

        // Alimenta el ConvoyAnalyzer con todo, no solo New/MacRotated: lo
        // que le importa es "en qué contexto se vio este fingerprint", y
        // eso pasa en cada paquete, no solo cuando cambia de estado.
        convoyanalyzer::onSighting(fp.hash);
        if (convoyanalyzer::isSuspect(fp.hash)) {
            alertengine::trigger(signaturedb::AlertProfile::ConvoySuspect, fp.hash);
        }

        dirty = true;
    }
}

} // namespace

void setup()
{
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setRotation(1);
    canvas.createSprite(SCREEN_W, SCREEN_H);
    initColors();

    sdOk = initSdCard();
    sightingstore::begin(sdOk);
    alertengine::begin();
    convoyanalyzer::begin();
    blescanner::begin();
    blescanner::start();
}

void loop()
{
    M5Cardputer.update();
    handleKeyboard();
    handleG0();
    processSightings();
    alertengine::update();
    render();
}
