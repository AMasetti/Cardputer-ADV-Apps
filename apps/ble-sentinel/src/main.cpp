// BLE Sentinel: detector pasivo de dispositivos BLE por firma de
// manufacturer data. Ver docs/BLE_SENTINEL_SPEC.md para el diseño completo.
//
// v1 incluye: fingerprint resistente a rotación de MAC, SignatureDB con
// smart glasses (Meta/Luxottica, Snap), alertas de audio por categoría, y
// log de avistamientos a SD (timestamp relativo al boot, sin RTC todavía).
// Pendiente para próximas versiones: ConvoyAnalyzer, RoomSweepEngine,
// WiFiSniffer, ExportModule, soporte de RTC por Grove.
//
// Teclas:
//   m   silenciar/activar alertas de audio
//   +/- volumen master
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
constexpr int ROW_H = 11;
constexpr int LIST_TOP = 24;
constexpr int MAX_VISIBLE_ROWS = (SCREEN_H - LIST_TOP) / ROW_H;

M5Canvas canvas(&M5Cardputer.Display);
bool dirty = true;
String statusMsg;

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
            return confidenceLow ? TFT_ORANGE : TFT_RED;
        case signaturedb::Category::Headset:
            return TFT_DARKGREY;
        default:
            return TFT_YELLOW; // sin match -> tratado como sospechoso, ver signature_db.h
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
    bool ok = SD.begin(SD_CS, SPI, 25000000);
    // Degradado explícito si falla: seguimos funcionando, solo que sin log
    // persistente ni session_id continuo (ver sighting_store.h).
    statusMsg = ok ? "SD ok" : "SD ausente: sin log";
    return ok;
}

void drawHeader()
{
    canvas.fillRect(0, 0, SCREEN_W, LIST_TOP, TFT_NAVY);
    canvas.setTextColor(TFT_WHITE, TFT_NAVY);
    canvas.setTextSize(1);
    canvas.setCursor(4, 2);
    canvas.printf("BLE Sentinel v%s  [%s]", APP_VERSION,
                   blescanner::scanning() ? "scan" : "stop");
    canvas.setCursor(4, 12);
    canvas.printf("audio:%s vol:%u  %s", alertengine::enabled() ? "on" : "off",
                   alertengine::masterVolume(), statusMsg.c_str());
}

void drawList()
{
    canvas.fillRect(0, LIST_TOP, SCREEN_W, SCREEN_H - LIST_TOP, TFT_BLACK);

    size_t n = sightingstore::count();
    // Copiamos índices y ordenamos por RSSI desc (más cerca arriba) sin
    // tocar el store -- es solo para esta pasada de dibujo.
    std::vector<size_t> order(n);
    for (size_t i = 0; i < n; i++) order[i] = i;
    std::sort(order.begin(), order.end(), [](size_t a, size_t b) {
        return sightingstore::byIndex(a)->lastRssi > sightingstore::byIndex(b)->lastRssi;
    });

    canvas.setTextSize(1);
    size_t shown = std::min<size_t>(order.size(), MAX_VISIBLE_ROWS);
    for (size_t row = 0; row < shown; row++) {
        const sightingstore::Record *r = sightingstore::byIndex(order[row]);
        if (!r) continue;
        int y = LIST_TOP + row * ROW_H;
        canvas.setTextColor(categoryColor(r->category, r->confidenceLow), TFT_BLACK);
        canvas.setCursor(2, y);
        canvas.printf("%-8s %4ddBm x%-3u %s%s", categoryLabel(r->category),
                       r->lastRssi, r->timesSeen, r->lastMac.c_str(),
                       r->confidenceLow ? " ?" : "");
    }

    if (n == 0) {
        canvas.setTextColor(TFT_DARKGREY, TFT_BLACK);
        canvas.setCursor(4, LIST_TOP + 4);
        canvas.print("Escaneando...");
    }
}

void render()
{
    if (!dirty) return;
    drawHeader();
    drawList();
    canvas.pushSprite(0, 0);
    dirty = false;
}

void handleKeyboard()
{
    if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) return;
    auto st = M5Cardputer.Keyboard.keysState();
    for (char c : st.word) {
        if (c == 'm') {
            alertengine::setEnabled(!alertengine::enabled());
            dirty = true;
        } else if (c == '+') {
            alertengine::setMasterVolume(std::min(255, alertengine::masterVolume() + 20));
            dirty = true;
        } else if (c == '-') {
            alertengine::setMasterVolume(std::max(0, alertengine::masterVolume() - 20));
            dirty = true;
        }
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
            dirty = true;
        }
    }
}

} // namespace

void setup()
{
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setRotation(1);
    canvas.createSprite(SCREEN_W, SCREEN_H);

    bool sdReady = initSdCard();
    sightingstore::begin(sdReady);
    alertengine::begin();
    blescanner::begin();
    blescanner::start();

    statusMsg += sightingstore::sdAvailable()
                     ? (" ses#" + String(sightingstore::sessionId()))
                     : "";
}

void loop()
{
    M5Cardputer.update();
    handleKeyboard();
    processSightings();
    alertengine::update();
    render();
}
