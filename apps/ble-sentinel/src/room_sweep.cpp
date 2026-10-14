#include "room_sweep.h"

namespace roomsweep {

namespace {

// Ventana corta a propósito: esto es para "¿me estoy acercando ahora?",
// no para tendencias de largo plazo (eso es trabajo de ConvoyAnalyzer).
constexpr size_t HISTORY_LEN = 8;
int8_t history[HISTORY_LEN];
size_t historyCount = 0;

uint64_t g_followedHash = 0;
bool g_following = false;

} // namespace

void begin()
{
    g_following = false;
    g_followedHash = 0;
    historyCount = 0;
}

void follow(uint64_t fingerprintHash)
{
    g_followedHash = fingerprintHash;
    g_following = true;
    historyCount = 0;
}

void stop() { g_following = false; }
bool isFollowing() { return g_following; }
uint64_t followedHash() { return g_followedHash; }

void sample(uint64_t fingerprintHash, int8_t rssi)
{
    if (!g_following || fingerprintHash != g_followedHash) return;

    if (historyCount < HISTORY_LEN) {
        history[historyCount++] = rssi;
    } else {
        // Buffer lleno: shiftea. HISTORY_LEN es chico (8), este costo es
        // insignificante frente al ritmo real de advertising.
        for (size_t i = 1; i < HISTORY_LEN; i++) history[i - 1] = history[i];
        history[HISTORY_LEN - 1] = rssi;
    }
}

int8_t smoothedRssi()
{
    if (historyCount == 0) return 0;
    int32_t sum = 0;
    for (size_t i = 0; i < historyCount; i++) sum += history[i];
    return static_cast<int8_t>(sum / static_cast<int32_t>(historyCount));
}

Trend trend()
{
    if (historyCount < 4) return Trend::Stable; // poca data, no afirmamos nada

    size_t half = historyCount / 2;
    int32_t oldSum = 0, newSum = 0;
    for (size_t i = 0; i < half; i++) oldSum += history[i];
    for (size_t i = historyCount - half; i < historyCount; i++) newSum += history[i];
    int32_t oldAvg = oldSum / static_cast<int32_t>(half);
    int32_t newAvg = newSum / static_cast<int32_t>(half);

    // RSSI menos negativo = señal más fuerte = más cerca.
    int32_t delta = newAvg - oldAvg;
    constexpr int32_t DEAD_ZONE_DBM = 3; // ruido normal de RF, no es tendencia real
    if (delta > DEAD_ZONE_DBM) return Trend::Closer;
    if (delta < -DEAD_ZONE_DBM) return Trend::Farther;
    return Trend::Stable;
}

} // namespace roomsweep
