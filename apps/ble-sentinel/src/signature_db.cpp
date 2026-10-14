#include "signature_db.h"

namespace signaturedb {

namespace {

const Entry TABLE[] = {
    // Luxottica (Ray-Ban Meta, Oakley Meta). Reportado por NoPeek y el
    // artículo de ZuckOff; NoPeek y "Nearby Glasses" coinciden en este valor.
    {0x0D53, Category::SmartGlasses, AlertProfile::SuspiciousDefault, "Luxottica (Ray-Ban/Oakley Meta)", true},

    // Meta Platforms Technologies (Quest y otros wearables Meta).
    {0x058E, Category::SmartGlasses, AlertProfile::SuspiciousDefault, "Meta Platforms Technologies", true},

    // Reportado por el proyecto "Nearby Glasses" como Meta Platforms, pero
    // un comentario de usuarios de NoPeek lo pone en duda -> sin verificar.
    {0x01AB, Category::SmartGlasses, AlertProfile::SuspiciousDefault, "Meta Platforms (sin confirmar)", false},

    // Snap Inc. (Spectacles), reportado por "Nearby Glasses".
    {0x03C2, Category::SmartGlasses, AlertProfile::SuspiciousDefault, "Snap Inc. (Spectacles)", false},
};

constexpr size_t TABLE_LEN = sizeof(TABLE) / sizeof(TABLE[0]);

// Substrings típicos de nombre de dispositivo para auriculares/headsets.
// Heurística declarada en signature_db.h: no reemplaza una tabla de company
// ID real, es un placeholder para tener un segundo AlertProfile funcionando.
const char *const HEADSET_NAME_HINTS[] = {
    "airpods", "buds", "headset", "headphone", "earphone", "wf-", "wh-",
};
constexpr size_t HEADSET_HINTS_LEN = sizeof(HEADSET_NAME_HINTS) / sizeof(HEADSET_NAME_HINTS[0]);

} // namespace

const Entry *matchByCompanyId(uint16_t companyId)
{
    for (size_t i = 0; i < TABLE_LEN; i++) {
        if (TABLE[i].companyId == companyId) return &TABLE[i];
    }
    return nullptr;
}

bool matchHeadsetByName(const String &localName)
{
    if (localName.isEmpty()) return false;
    String lower = localName;
    lower.toLowerCase();
    for (size_t i = 0; i < HEADSET_HINTS_LEN; i++) {
        if (lower.indexOf(HEADSET_NAME_HINTS[i]) >= 0) return true;
    }
    return false;
}

} // namespace signaturedb
