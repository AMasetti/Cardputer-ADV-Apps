// "Seguir" un dispositivo puntual vía RSSI.
//
// Es nomás "más cerca / más lejos" (más fuerte = más cerca). NO es
// triangulación ni tiene información de dirección -- por eso la UI usa un
// número grande + sparkline, no un radar: un radar sugiere posición/ángulo
// que no podemos conocer, y eso termina siendo una interfaz engañosa para
// lo que en realidad es una sola dimensión (distancia aproximada).
//
// RSSI de BLE real salta 10-15dBm aunque el dispositivo esté quieto y
// cerca (multipath, orientación de antena, el propio cuerpo bloqueando la
// señal) -- eso es física de RF, no un bug. v1 calculaba tendencia
// comparando la primera mitad contra la segunda mitad de solo 8 muestras
// crudas con una zona muerta de 3dBm: con ese nivel de ruido normal,
// cualquier paquete puntual alcanzaba para voltear el resultado (decía
// "acercándose" estando lejos y viceversa). v2 usa dos promedios móviles
// exponenciales (rápido y lento, ver .cpp) y compara la diferencia entre
// ellos -- un solo paquete ruidoso ya no alcanza para mover la aguja,
// hace falta una tendencia sostenida de varios paquetes.
//
// También ofrece un modo de tono continuo (frecuencia sube cuanto más
// cerca, estilo detector de metales) para buscar sin tener que mirar la
// pantalla -- toque corto de G0 lo prende/apaga mientras se está siguiendo
// un dispositivo; respeta el mute/volumen global de alert_engine.
//
// Ver docs/BLE_SENTINEL_SPEC.md sección 3.
#pragma once

#include <Arduino.h>

namespace roomsweep {

enum class Trend : int8_t { Closer = 1, Stable = 0, Farther = -1 };

void begin();

// Arranca a seguir este fingerprint: limpia el historial anterior. Llamar
// al entrar a la pantalla de seguimiento.
void follow(uint64_t fingerprintHash);
void stop(); // deja de seguir (pantalla cerrada) -- también apaga el tono
bool isFollowing();
uint64_t followedHash();

// Alimentar con cada avistamiento del fingerprint seguido (desde
// processSightings en main.cpp). No hace nada si no coincide con
// followedHash() o si no se está siguiendo nada.
void sample(uint64_t fingerprintHash, int8_t rssi);

// EMA rápida (ver .cpp) -- se mueve más que un promedio de ventana larga
// pero no tiembla con cada paquete individual como el RSSI crudo.
int8_t smoothedRssi();
Trend trend();

// Historial crudo (sin promediar) para el sparkline. Índice 0 = muestra
// más vieja de la ventana, historyCount()-1 = la más nueva.
size_t historyCount();
int8_t historyAt(size_t i);

// Modo de tono continuo: toggle con G0 corto en main.cpp mientras se sigue
// un dispositivo. updateTone() hay que llamarla en cada loop() -- no hace
// nada si el modo está apagado, si no se está siguiendo nada, o si el
// audio global está en mute (ver alert_engine).
void setToneMode(bool on);
bool toneMode();
void updateTone();

} // namespace roomsweep
