#include "room_sweep.h"
#include <M5Cardputer.h>
#include "alert_engine.h"

namespace roomsweep {

namespace {

// Ventana corta a propósito: esto es para "¿me estoy acercando ahora?",
// no para tendencias de largo plazo (eso es trabajo de ConvoyAnalyzer).
constexpr size_t HISTORY_LEN = 8;
int8_t g_history[HISTORY_LEN];
size_t g_historyCount = 0;

uint64_t g_followedHash = 0;
bool g_following = false;

bool g_toneMode = false;
bool g_toneActive = false;
uint16_t g_lastToneFreq = 0;
uint32_t g_lastToneUpdateMs = 0;
constexpr uint32_t TONE_UPDATE_INTERVAL_MS = 120;
constexpr uint16_t TONE_FREQ_MIN = 300;  // -95dBm, lejos
constexpr uint16_t TONE_FREQ_MAX = 1800; // -40dBm, cerca

void silenceTone()
{
    if (g_toneActive) {
        M5Cardputer.Speaker.stop();
        g_toneActive = false;
    }
}

} // namespace

void begin()
{
    g_following = false;
    g_followedHash = 0;
    g_historyCount = 0;
    g_toneMode = false;
    silenceTone();
}

void follow(uint64_t fingerprintHash)
{
    g_followedHash = fingerprintHash;
    g_following = true;
    g_historyCount = 0;
}

void stop()
{
    g_following = false;
    g_toneMode = false; // cada vez que se vuelve a seguir algo, arranca en silencio
    silenceTone();
}

bool isFollowing() { return g_following; }
uint64_t followedHash() { return g_followedHash; }

void sample(uint64_t fingerprintHash, int8_t rssi)
{
    if (!g_following || fingerprintHash != g_followedHash) return;

    if (g_historyCount < HISTORY_LEN) {
        g_history[g_historyCount++] = rssi;
    } else {
        // Buffer lleno: shiftea. HISTORY_LEN es chico (8), este costo es
        // insignificante frente al ritmo real de advertising.
        for (size_t i = 1; i < HISTORY_LEN; i++) g_history[i - 1] = g_history[i];
        g_history[HISTORY_LEN - 1] = rssi;
    }
}

int8_t smoothedRssi()
{
    if (g_historyCount == 0) return 0;
    int32_t sum = 0;
    for (size_t i = 0; i < g_historyCount; i++) sum += g_history[i];
    return static_cast<int8_t>(sum / static_cast<int32_t>(g_historyCount));
}

Trend trend()
{
    if (g_historyCount < 4) return Trend::Stable; // poca data, no afirmamos nada

    size_t half = g_historyCount / 2;
    int32_t oldSum = 0, newSum = 0;
    for (size_t i = 0; i < half; i++) oldSum += g_history[i];
    for (size_t i = g_historyCount - half; i < g_historyCount; i++) newSum += g_history[i];
    int32_t oldAvg = oldSum / static_cast<int32_t>(half);
    int32_t newAvg = newSum / static_cast<int32_t>(half);

    // RSSI menos negativo = señal más fuerte = más cerca.
    int32_t delta = newAvg - oldAvg;
    constexpr int32_t DEAD_ZONE_DBM = 3; // ruido normal de RF, no es tendencia real
    if (delta > DEAD_ZONE_DBM) return Trend::Closer;
    if (delta < -DEAD_ZONE_DBM) return Trend::Farther;
    return Trend::Stable;
}

size_t historyCount() { return g_historyCount; }

int8_t historyAt(size_t i)
{
    if (i >= g_historyCount) return 0;
    return g_history[i];
}

void setToneMode(bool on)
{
    g_toneMode = on;
    if (!on) silenceTone();
}

bool toneMode() { return g_toneMode; }

void updateTone()
{
    if (!g_toneMode || !g_following || !alertengine::enabled()) {
        silenceTone();
        return;
    }

    uint32_t now = millis();
    if (now - g_lastToneUpdateMs < TONE_UPDATE_INTERVAL_MS) return;
    g_lastToneUpdateMs = now;

    int rssi = smoothedRssi();
    int rssiClamped = rssi < -95 ? -95 : (rssi > -40 ? -40 : rssi);
    uint16_t freq = TONE_FREQ_MIN + (rssiClamped + 95) * (TONE_FREQ_MAX - TONE_FREQ_MIN) / 55;

    // Solo retrigger el tono si cambió lo suficiente -- reiniciar la forma
    // de onda cada 120ms con la misma frecuencia solo produce clicks.
    int delta = static_cast<int>(freq) - static_cast<int>(g_lastToneFreq);
    if (!g_toneActive || delta > 15 || delta < -15) {
        M5Cardputer.Speaker.setVolume(alertengine::masterVolume());
        M5Cardputer.Speaker.tone(freq, 0); // duración 0 = continuo hasta stop()
        g_lastToneFreq = freq;
        g_toneActive = true;
    }
}

} // namespace roomsweep
