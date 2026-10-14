#include "sighting_store.h"
#include <SD.h>
#include "fingerprint.h"

namespace sightingstore {

namespace {

constexpr const char *DATA_DIR = "/sentinel";
constexpr const char *HISTORY_DIR = "/sentinel/history";
constexpr const char *SESSION_FILE = "/sentinel/session.txt";

std::vector<Record> records;
uint32_t g_sessionId = 0;
bool g_sdAvailable = false;
File historyLog;

const char *categoryLabel(signaturedb::Category c)
{
    switch (c) {
        case signaturedb::Category::SmartGlasses: return "glasses";
        case signaturedb::Category::DroneRemoteId: return "drone";
        case signaturedb::Category::Headset: return "headset";
        default: return "unknown";
    }
}

// session.txt guarda un único entero: el próximo session_id a usar. Si no
// existe (primera vez, o no hay SD) arrancamos en 1.
uint32_t loadAndBumpSessionId()
{
    uint32_t id = 1;
    if (SD.exists(SESSION_FILE)) {
        File f = SD.open(SESSION_FILE);
        if (f) {
            id = f.parseInt();
            f.close();
            if (id == 0) id = 1;
        }
    }
    File f = SD.open(SESSION_FILE, FILE_WRITE);
    if (f) {
        f.print(id + 1);
        f.close();
    }
    return id;
}

void openHistoryLog()
{
    char path[48];
    snprintf(path, sizeof(path), "%s/session_%lu.log", HISTORY_DIR,
              static_cast<unsigned long>(g_sessionId));
    historyLog = SD.open(path, FILE_WRITE);
}

// Una línea JSON simple por evento loggeable. No parseamos JSON en el
// dispositivo (ver signature_db.h) pero escribirlo en ese formato deja el
// archivo listo para herramientas externas (export, análisis) sin tener que
// definir un formato propio.
void appendLogLine(const Record &r, SightingKind kind)
{
    if (!historyLog) return;
    const char *kindStr = kind == SightingKind::New ? "new"
                          : kind == SightingKind::MacRotated ? "mac_rotated"
                                                              : "update";
    historyLog.printf(
        "{\"session_rel_ms\":%lu,\"fp\":\"%016llx\",\"mac\":\"%s\",\"rssi\":%d,"
        "\"category\":\"%s\",\"kind\":\"%s\",\"confidence_low\":%s}\n",
        static_cast<unsigned long>(r.lastSeenMs),
        static_cast<unsigned long long>(r.fingerprintHash),
        r.lastMac.c_str(), r.lastRssi, categoryLabel(r.category), kindStr,
        r.confidenceLow ? "true" : "false");
    historyLog.flush();
}

Record *find(uint64_t hash)
{
    for (auto &r : records) {
        if (r.fingerprintHash == hash) return &r;
    }
    return nullptr;
}

} // namespace

void begin(bool sdReady)
{
    g_sdAvailable = sdReady;
    if (g_sdAvailable) {
        if (!SD.exists(DATA_DIR)) SD.mkdir(DATA_DIR);
        if (!SD.exists(HISTORY_DIR)) SD.mkdir(HISTORY_DIR);
        g_sessionId = loadAndBumpSessionId();
        openHistoryLog();
    } else {
        g_sessionId = 1; // degradado: solo RAM, sin continuidad entre boots
    }
    records.clear();
    records.reserve(32);
}

uint32_t sessionId() { return g_sessionId; }
bool sdAvailable() { return g_sdAvailable; }

SightingKind record(uint64_t fingerprintHash, const String &mac, int8_t rssi,
                     signaturedb::Category category, bool confidenceLow,
                     Record &out)
{
    uint32_t now = millis();
    Record *existing = find(fingerprintHash);

    if (!existing) {
        Record r;
        r.fingerprintHash = fingerprintHash;
        r.lastMac = mac;
        r.lastRssi = rssi;
        r.firstSeenMs = now;
        r.lastSeenMs = now;
        r.timesSeen = 1;
        r.macChanges = 0;
        r.category = category;
        r.confidenceLow = confidenceLow;
        records.push_back(r);
        out = r;
        appendLogLine(out, SightingKind::New);
        return SightingKind::New;
    }

    bool macChanged = (existing->lastMac != mac);
    SightingKind kind = SightingKind::Update;
    if (macChanged) {
        uint32_t delta = now - existing->lastSeenMs;
        // Solo lo tratamos como rotación "plausible" (y lo loggeamos como tal)
        // si el intervalo es compatible; si no, es simplemente ruido de
        // RSSI/timing y lo anotamos igual pero sin la etiqueta especial.
        kind = fingerprint::isPlausibleMacRotation(delta) ? SightingKind::MacRotated
                                                           : SightingKind::Update;
        existing->macChanges++;
    }

    existing->lastMac = mac;
    existing->lastRssi = rssi;
    existing->lastSeenMs = now;
    existing->timesSeen++;
    out = *existing;

    // No logueamos cada Update (saturaría la SD con el ritmo de advertising
    // típico, cada ~100-1000ms) -- solo eventos que importan para el
    // historial: nuevo dispositivo o rotación de MAC detectada.
    if (kind != SightingKind::Update) appendLogLine(out, kind);

    return kind;
}

size_t count() { return records.size(); }

const Record *byIndex(size_t i)
{
    if (i >= records.size()) return nullptr;
    return &records[i];
}

const Record *byFingerprint(uint64_t fingerprintHash)
{
    return find(fingerprintHash);
}

} // namespace sightingstore
