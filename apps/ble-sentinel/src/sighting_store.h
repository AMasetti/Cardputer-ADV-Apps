// Registro de avistamientos por fingerprint, con log a SD.
//
// v1 sin RTC (decisión tomada en la conversación: se agrega más adelante
// por Grove/I2C). Todo timestamp es `millis()` relativo al boot de ESTA
// sesión, no hora de pared. Eso implica:
//   - "recurrente, mismo lugar" funciona (compara dentro de la sesión).
//   - "recurrente entre sesiones separadas por un apagado" NO funciona
//     todavía: cada boot es una sesión nueva sin forma de saber cuánto
//     tiempo pasó entre una y otra. Queda explícitamente fuera de alcance
//     hasta que el RTC esté.
// Cuando llegue el RTC, se suma un campo `wallClockMs` opcional (poblado
// solo si `rtcAvailable`) sin tocar lo que ya hay.
#pragma once

#include <Arduino.h>
#include <vector>
#include "signature_db.h"

namespace sightingstore {

struct Record {
    uint64_t fingerprintHash = 0;
    String lastMac;
    int8_t lastRssi = 0;
    uint32_t firstSeenMs = 0;  // relativo a begin() de esta sesión
    uint32_t lastSeenMs = 0;
    uint16_t timesSeen = 0;
    uint8_t macChanges = 0;    // cuántas MAC distintas vimos para este fingerprint
    signaturedb::Category category = signaturedb::Category::Unknown;
    bool confidenceLow = false;

    // Crudo del último advertising packet, para la pantalla de detalle --
    // resolve() en main.cpp ya consume esto para categorizar, pero antes
    // se perdía: sin guardarlo acá no había forma de mostrar "fabricante"
    // o "nombre anunciado" después del hecho.
    uint16_t lastCompanyId = 0xFFFF; // 0xFFFF = sin manufacturer data
    int8_t lastTxPowerDbm = INT8_MIN; // INT8_MIN = no declarado
    uint8_t lastPayloadLen = 0;
    String lastLocalName; // vacío si el dispositivo no lo anunció
};

// Resultado de registrar un avistamiento, para que el caller sepa si vale
// la pena alertar/loggear o es solo una repetición reciente.
enum class SightingKind : uint8_t {
    New,         // primera vez que vemos este fingerprint en esta sesión
    MacRotated,  // mismo fingerprint, MAC distinta, delta compatible con rotación
    Update,      // mismo fingerprint y MAC, solo actualiza RSSI/timestamp
};

// Inicializa la sesión: monta /sentinel en SD (si `sdReady`, resultado de
// que SD.begin() haya funcionado en main.cpp) y asigna session_id
// incremental persistido en /sentinel/session.txt. Si no hay SD, sigue
// funcionando solo en RAM (degradado, sin log persistente).
void begin(bool sdReady);

uint32_t sessionId();
bool sdAvailable();

// Entrada para record(): identidad resuelta (fingerprint/categoría, ya
// decidida por el caller) + los campos crudos del advertising packet que
// vale la pena guardar para la pantalla de detalle.
struct SightingInput {
    uint64_t fingerprintHash;
    String mac;
    int8_t rssi;
    signaturedb::Category category;
    bool confidenceLow;
    uint16_t companyId;
    int8_t txPowerDbm;
    uint8_t payloadLen;
    String localName;
};

// Registra/actualiza un avistamiento. Devuelve el tipo de evento y, por
// out-param, el Record actualizado.
SightingKind record(const SightingInput &in, Record &out);

size_t count();
const Record *byIndex(size_t i); // para listar en UI, ordenado por lastSeenMs desc
const Record *byFingerprint(uint64_t fingerprintHash); // para seguir un dispositivo puntual (radar)

} // namespace sightingstore
