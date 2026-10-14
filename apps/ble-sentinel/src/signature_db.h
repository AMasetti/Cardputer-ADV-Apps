// Tabla de firmas conocidas: company ID de manufacturer data -> categoría.
//
// v1: tabla compilada en el binario (sin parseo de JSON en runtime, el repo
// no usa ArduinoJson en ningún lado y no queríamos sumar esa dependencia
// solo para esto). Está pensada para poder migrar a un archivo en SD más
// adelante sin tocar los callers: toda la lógica pasa por match(), que hoy
// mira el array estático y el día de mañana puede mirar algo cargado de SD.
//
// Fuentes de los company IDs (ver conversación / docs/BLE_SENTINEL_SPEC.md):
// proyectos NoPeek y "Nearby Glasses" reportan estos valores para smart
// glasses con cámara. Hay discrepancias menores entre ambos proyectos sobre
// algunos de ellos -> quedan marcados `verified = false` hasta confirmarlos
// contra una captura real o el registro oficial del Bluetooth SIG.
#pragma once

#include <Arduino.h>

namespace signaturedb {

enum class Category : uint8_t {
    Unknown,       // sin match -> tratado como sospechoso por defecto
    SmartGlasses,  // Meta Ray-Ban/Oakley, Snap Spectacles, etc.
    DroneRemoteId, // ASTM F3411 Remote ID (matcheado por service UUID, no company ID)
    Headset,       // heurística por nombre, ver matchByName()
};

// Nombres de perfil de alerta que consume alert_engine. Separado de
// Category porque a futuro una misma categoría podría tener distintos
// perfiles según confianza/contexto.
enum class AlertProfile : uint8_t {
    SuspiciousDefault,
    HeadsetLowPriority,
    Silent,
    ConvoySuspect, // disparado por convoy_analyzer, no por match de firma
};

struct Entry {
    uint16_t companyId;
    Category category;
    AlertProfile alertProfile;
    const char *vendorLabel;
    bool verified; // false = reportado por terceros, no confirmado por nosotros
};

// Busca por company ID de manufacturer data. Devuelve nullptr si no hay
// match -> el caller debe tratarlo como Category::Unknown / SuspiciousDefault
// (postura conservadora: lo no identificado es lo más prioritario, no lo
// menos, ver spec sección AlertEngine).
const Entry *matchByCompanyId(uint16_t companyId);

// Heurística v1 para auriculares: substring en el nombre del dispositivo
// (AD type 0x09, Complete/Shortened Local Name). No es tan confiable como
// un company ID -especialmente porque muchos fabricantes también usan esos
// mismos nombres genéricos para otra cosa- pero sirve como segundo perfil
// de alerta real para probar el AlertEngine mientras no tenemos una tabla
// de company IDs de auriculares verificada.
bool matchHeadsetByName(const String &localName);

} // namespace signaturedb
