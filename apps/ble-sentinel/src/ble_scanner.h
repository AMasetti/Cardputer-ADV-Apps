// Wrapper de NimBLE en modo scan-only (observer).
//
// El rol scan-only está forzado en dos capas, no solo acá:
//   1. platformio.ini deshabilita los roles PERIPHERAL/BROADCASTER/CENTRAL
//      a nivel de compilación (CONFIG_BT_NIMBLE_ROLE_*_DISABLED) -> ni este
//      código ni ningún otro puede anunciar ni conectar aunque lo intente.
//   2. Este wrapper nunca llama a startAdvertising() ni a ningún método de
//      conexión, por diseño.
// Ver docs/BLE_SENTINEL_SPEC.md sección "Auto-ocultamiento".
#pragma once

#include <Arduino.h>
#include <vector>

namespace blescanner {

// Un avistamiento crudo, ya extraído del adv packet, listo para pasarle a
// fingerprint::compute() y signaturedb::matchByCompanyId().
struct RawSighting {
    String mac;
    int8_t rssi = 0;
    uint16_t companyId = 0xFFFF; // 0xFFFF = sin manufacturer data
    uint8_t payloadLen = 0;
    int8_t txPowerDbm = INT8_MIN;
    std::vector<uint16_t> serviceUuids16; // ordenados
    String localName;                     // puede venir vacío
};

void begin();

// Escaneo continuo (no por duración fija): arranca una vez y sigue hasta
// stop(). Igual que shell-rc pero sin filtro de nombre y sin timeout.
void start();
void stop();
bool scanning();

// Llamar en loop(): drena lo que juntó el callback de NimBLE (que corre en
// su propia tarea) hacia `out`, protegido por mutex. Devuelve cuántos
// avistamientos nuevos copió.
size_t poll(std::vector<RawSighting> &out);

} // namespace blescanner
