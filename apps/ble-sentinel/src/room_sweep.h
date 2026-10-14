// "Seguir" un dispositivo puntual vía RSSI -- la realización concreta de
// RoomSweepEngine de la spec (sección 3), en versión "radar": no es
// triangulación real (necesitaría múltiples antenas o referencia espacial),
// es nomás "más cerca / más lejos" con una ventana corta de RSSI para
// suavizar el ruido y calcular tendencia. La UI (main.cpp) lo deja claro
// con un cartel, no se vende como posición exacta.
//
// Ver docs/BLE_SENTINEL_SPEC.md sección 3.
#pragma once

#include <Arduino.h>

namespace roomsweep {

enum class Trend : int8_t { Closer = 1, Stable = 0, Farther = -1 };

void begin();

// Arranca a seguir este fingerprint: limpia el historial anterior. Llamar
// al entrar a la pantalla de radar.
void follow(uint64_t fingerprintHash);
void stop(); // deja de seguir (pantalla de radar cerrada)
bool isFollowing();
uint64_t followedHash();

// Alimentar con cada avistamiento del fingerprint seguido (desde
// processSightings en main.cpp). No hace nada si no coincide con
// followedHash() o si no se está siguiendo nada.
void sample(uint64_t fingerprintHash, int8_t rssi);

// Promedio corto (últimas N muestras) para que el radar no tiemble con
// cada paquete individual.
int8_t smoothedRssi();
Trend trend();

} // namespace roomsweep
