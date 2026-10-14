#include "alert_engine.h"
#include <M5Cardputer.h>

namespace alertengine {

namespace {

// Defaults v1 acordados en la conversación: sospechoso a volumen pleno,
// auricular a volumen bajo, confiado en silencio.
const Profile PROFILES[] = {
    /* SuspiciousDefault  */ {100, 1200, 160, 2, 60000},
    /* HeadsetLowPriority */ {35, 800, 160, 1, 60000},
    /* Silent             */ {0, 0, 0, 0, 0},
};

const Profile &profileFor(signaturedb::AlertProfile p)
{
    return PROFILES[static_cast<uint8_t>(p)];
}

// Cooldown por fingerprint: tabla fija, se pisa la entrada más vieja cuando
// se llena. No necesitamos más que "no repetir seguido", así que un mapa
// exacto sin límite de tamaño sería sobrekill para un microcontrolador.
constexpr size_t COOLDOWN_SLOTS = 64;
struct CooldownSlot {
    uint64_t hash = 0;
    uint32_t lastTriggerMs = 0;
    bool used = false;
};
CooldownSlot cooldowns[COOLDOWN_SLOTS];
size_t nextSlot = 0;

bool withinCooldown(uint64_t hash, uint32_t cooldownMs, uint32_t nowMs)
{
    for (auto &slot : cooldowns) {
        if (slot.used && slot.hash == hash) {
            return (nowMs - slot.lastTriggerMs) < cooldownMs;
        }
    }
    return false;
}

void recordTrigger(uint64_t hash, uint32_t nowMs)
{
    for (auto &slot : cooldowns) {
        if (slot.used && slot.hash == hash) {
            slot.lastTriggerMs = nowMs;
            return;
        }
    }
    cooldowns[nextSlot] = CooldownSlot{hash, nowMs, true};
    nextSlot = (nextSlot + 1) % COOLDOWN_SLOTS;
}

// Estado del patrón de beeps en curso, avanzado por update().
struct PendingPattern {
    bool active = false;
    Profile profile;
    uint8_t beepsLeft = 0;
    uint32_t nextActionMs = 0;
    bool toneOn = false;
};
PendingPattern pending;

bool g_enabled = true;
uint8_t g_masterVolume = 180; // 0-255, mismo rango que M5Cardputer.Speaker

void startPattern(const Profile &p)
{
    pending.active = true;
    pending.profile = p;
    pending.beepsLeft = p.beepCount;
    pending.nextActionMs = millis();
    pending.toneOn = false;
}

} // namespace

void begin()
{
    M5Cardputer.Speaker.begin();
    M5Cardputer.Speaker.setVolume(g_masterVolume);
}

void setEnabled(bool on) { g_enabled = on; }
bool enabled() { return g_enabled; }

void setMasterVolume(uint8_t v)
{
    g_masterVolume = v;
    M5Cardputer.Speaker.setVolume(g_masterVolume);
}
uint8_t masterVolume() { return g_masterVolume; }

void trigger(signaturedb::AlertProfile profileId, uint64_t fingerprintHash)
{
    if (!g_enabled) return;

    const Profile &p = profileFor(profileId);
    if (p.volumePct == 0 || p.beepCount == 0) return; // perfil Silent

    uint32_t now = millis();
    if (withinCooldown(fingerprintHash, p.cooldownMs, now)) return;
    recordTrigger(fingerprintHash, now);

    startPattern(p);
}

void update()
{
    if (!pending.active) return;

    uint32_t now = millis();
    if (now < pending.nextActionMs) return;

    const Profile &p = pending.profile;

    if (!pending.toneOn) {
        if (pending.beepsLeft == 0) {
            pending.active = false;
            return;
        }
        // Volumen efectivo = master * volumePct del perfil. setVolume toma
        // 0-255, igual que el resto de la app (ver tuner/mp3-player).
        uint8_t effectiveVolume = (uint16_t)g_masterVolume * p.volumePct / 100;
        M5Cardputer.Speaker.setVolume(effectiveVolume);
        M5Cardputer.Speaker.tone(p.toneFreqHz, p.toneDurationMs);
        pending.toneOn = true;
        pending.nextActionMs = now + p.toneDurationMs;
        pending.beepsLeft--;
    } else {
        // Pausa entre beeps, mismo largo que el tono para un patrón parejo.
        pending.toneOn = false;
        pending.nextActionMs = now + p.toneDurationMs;
        if (pending.beepsLeft == 0) {
            // Restaurar volumen master para que no quede pisado para otros usos.
            M5Cardputer.Speaker.setVolume(g_masterVolume);
        }
    }
}

} // namespace alertengine
