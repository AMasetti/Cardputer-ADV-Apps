// BLE Sentinel: detector pasivo de dispositivos BLE por firma de
// manufacturer data. Ver docs/BLE_SENTINEL_SPEC.md para el diseño completo.
//
// v1 incluye: fingerprint resistente a rotación de MAC, SignatureDB con
// smart glasses (Meta/Luxottica, Snap), alertas de audio por categoría,
// log de avistamientos a SD (timestamp relativo al boot, sin RTC todavía),
// ConvoyAnalyzer (seguimiento sostenido entre "contextos" marcados a mano)
// y RoomSweep (seguir un dispositivo puntual por RSSI -- número grande +
// sparkline + tono continuo opcional, NO un radar: un radar implica
// dirección/posición que no tenemos, ver room_sweep.h) + ExportModule
// (CSV/JSON con hash de integridad a SD).
// Pendiente para próximas versiones: WiFiSniffer, soporte de RTC por
// Grove, más firmas (drones, trackers).
//
// Flujo de pantallas: Lista -> (enter, con scan pausado) -> Detalle ->
// (enter) -> Seguimiento. `del` siempre vuelve un paso atrás
// (Seguimiento->Detalle, Detalle->Lista).
//
// Controles:
//   G0 mantenido ~0.8s    activar/silenciar alertas de audio, global (arranca en mute)
//   G0 corto (en Seguimiento) activar/desactivar el tono continuo de búsqueda
//   espacio               pausar/reanudar el scan
//   ; w  /  . s           (con el scan pausado) mover la selección en la lista
//   enter                 lista: abrir el detalle del seleccionado
//                         detalle: seguirlo (RSSI en vivo)
//   del                   volver un paso atrás (seguimiento <- detalle <- lista)
//   c                     marcar "cambié de lugar" (nuevo contexto, ver ConvoyAnalyzer)
//   e                     exportar lo visto hasta ahora (CSV + JSON + hash) a SD
//   +/-                   volumen master
#include <M5Cardputer.h>
#include <SD.h>
#include <SPI.h>
#include <vector>
#include <algorithm>
#include <math.h>

#include "fingerprint.h"
#include "signature_db.h"
#include "sighting_store.h"
#include "ble_scanner.h"
#include "alert_engine.h"
#include "convoy_analyzer.h"
#include "room_sweep.h"
#include "export_module.h"

#ifndef APP_VERSION
#define APP_VERSION "dev"
#endif

namespace {

constexpr int SD_SCK = 40;
constexpr int SD_MISO = 39;
constexpr int SD_MOSI = 14;
constexpr int SD_CS = 12;

constexpr int SCREEN_W = 240;
constexpr int SCREEN_H = 135;
constexpr int HUD_H = 15;
constexpr int STATUS_H = 13;
constexpr int ROW_H = 12;
constexpr int LIST_TOP = HUD_H + STATUS_H;
constexpr int MAX_VISIBLE_ROWS = (SCREEN_H - LIST_TOP) / ROW_H;

M5Canvas canvas(&M5Cardputer.Display);
bool dirty = true;
bool sdOk = false;

enum class Screen : uint8_t { List, Detail, Follow };
Screen screen = Screen::List;
int cursorIndex = 0;  // índice ABSOLUTO en sortedOrder(), no la fila visible
int scrollOffset = 0; // primer índice visible; se ajusta para que cursorIndex siempre se vea
uint64_t selectedFingerprint = 0; // dispositivo elegido, vive en Detail y Follow

String toastMsg;        // feedback corto (ej. "exportado: ...") en el footer
uint32_t toastUntilMs = 0;

// Paleta compartida con el resto del repo (shell-rc, led-rc): fondo casi
// negro, barra de HUD gris oscuro, acento rojo porque esto es una app de
// seguridad (alarma), verde/gris para "todo tranquilo".
uint16_t C_BG, C_HUD, C_TEXT, C_DIM, C_ACCENT, C_ON, C_OFF, C_WARN;

const char *categoryLabel(signaturedb::Category c)
{
    switch (c) {
        case signaturedb::Category::SmartGlasses: return "GLASSES";
        case signaturedb::Category::DroneRemoteId: return "DRONE";
        case signaturedb::Category::Headset: return "headset";
        case signaturedb::Category::FindMyTracker: return "FINDMY";
        default: return "unknown";
    }
}

uint16_t categoryColor(signaturedb::Category c, bool confidenceLow)
{
    switch (c) {
        case signaturedb::Category::SmartGlasses:
        case signaturedb::Category::DroneRemoteId:
        case signaturedb::Category::FindMyTracker:
            return confidenceLow ? C_WARN : C_ACCENT;
        case signaturedb::Category::Headset:
            return C_DIM;
        default:
            return C_ACCENT; // sin match -> tratado como sospechoso, ver signature_db.h
    }
}

// Resuelve categoría + perfil de alerta para un RawSighting ya mapeado a
// fingerprint. Único punto donde se decide "qué es esto", todo lo demás
// (UI, alertas, store) consume el resultado.
struct Resolved {
    signaturedb::Category category;
    signaturedb::AlertProfile alertProfile;
    bool confidenceLow;
};

// Toma companyId/localName "efectivos" (ver processSightings: el llamador
// ya los mezcló con lo que el Record ya sabía de paquetes anteriores), no
// el paquete crudo de este sighting puntual -- muchos dispositivos recién
// mandan el nombre en el scan response, no en el primer advertising, y
// si resolviéramos solo con el paquete de hoy un dispositivo identificado
// hace 3 paquetes volvería a Unknown en el próximo que no traiga nombre.
Resolved resolve(uint16_t effectiveCompanyId, uint8_t effectiveMfgTypeByte,
                  const String &effectiveLocalName, bool fpConfidenceLow)
{
    if (const signaturedb::Entry *e = signaturedb::matchByCompanyId(effectiveCompanyId)) {
        return {e->category, e->alertProfile, fpConfidenceLow || !e->verified};
    }
    // Find My antes que el heurístico de auriculares por nombre: un AirTag
    // no suele anunciar nombre, pero si lo hiciera, el type byte es mucho
    // más confiable que un substring de texto.
    if (signaturedb::matchFindMyNetwork(effectiveCompanyId, effectiveMfgTypeByte)) {
        return {signaturedb::Category::FindMyTracker, signaturedb::AlertProfile::SuspiciousDefault, fpConfidenceLow};
    }
    if (signaturedb::matchHeadsetByName(effectiveLocalName)) {
        return {signaturedb::Category::Headset, signaturedb::AlertProfile::HeadsetLowPriority, fpConfidenceLow};
    }
    // Sin match: postura conservadora, se trata como sospechoso por defecto
    // (ver signature_db.h / AlertEngine).
    return {signaturedb::Category::Unknown, signaturedb::AlertProfile::SuspiciousDefault, fpConfidenceLow};
}

bool initSdCard()
{
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    // Degradado explícito si falla: seguimos funcionando, solo que sin log
    // persistente ni session_id continuo (ver sighting_store.h).
    return SD.begin(SD_CS, SPI, 25000000);
}

void initColors()
{
    C_BG = canvas.color565(10, 10, 14);
    C_HUD = canvas.color565(32, 28, 30);
    C_TEXT = canvas.color565(225, 225, 230);
    C_DIM = canvas.color565(120, 120, 130);
    C_ACCENT = canvas.color565(221, 29, 33);   // rojo: esto es una alarma
    C_ON = canvas.color565(80, 210, 110);      // verde: audio activo / ok
    C_OFF = canvas.color565(50, 50, 58);
    C_WARN = canvas.color565(230, 160, 40);    // ámbar: firma sin verificar
}

// --- Dibujo ------------------------------------------------------------------------

void drawHud()
{
    canvas.fillRect(0, 0, SCREEN_W, HUD_H, C_HUD);
    canvas.setFont(&fonts::Font0);
    canvas.setTextSize(1);
    canvas.setTextColor(C_ACCENT, C_HUD);
    canvas.setTextDatum(middle_left);
    canvas.drawString("SENTINEL", 4, HUD_H / 2);
    canvas.setTextColor(C_TEXT, C_HUD);
    canvas.setTextDatum(middle_center);
    canvas.drawString(blescanner::scanning() ? "escaneando" : "detenido", SCREEN_W / 2, HUD_H / 2);
    canvas.setTextColor(C_DIM, C_HUD);
    canvas.setTextDatum(middle_right);
    canvas.drawString("v" APP_VERSION, SCREEN_W - 4, HUD_H / 2);
}

// Chip tipo "pill" igual al de shell-rc/led-rc: rótulo con fondo lleno
// cuando está "on", borde nomás cuando está "off".
void drawChip(int x, int y, int w, const char *label, bool on, uint16_t onColor)
{
    canvas.fillRoundRect(x, y, w, 11, 3, on ? onColor : C_BG);
    if (!on) canvas.drawRoundRect(x, y, w, 11, 3, C_DIM);
    canvas.setTextDatum(middle_center);
    canvas.setFont(&fonts::Font0);
    canvas.setTextColor(on ? C_BG : C_DIM, on ? onColor : C_BG);
    canvas.drawString(label, x + w / 2, y + 6);
}

void drawStatusBar()
{
    canvas.fillRect(0, HUD_H, SCREEN_W, STATUS_H, C_BG);
    int y = HUD_H + 1;
    drawChip(4, y, 46, alertengine::enabled() ? "AUDIO ON" : "MUTE", alertengine::enabled(), C_ON);
    drawChip(54, y, 36, sdOk ? "SD OK" : "SD --", sdOk, C_ON);

    canvas.setTextDatum(middle_left);
    canvas.setTextColor(C_DIM, C_BG);
    canvas.drawString(String("ctx ") + convoyanalyzer::currentContext(), 94, y + 6);

    canvas.setTextDatum(middle_right);
    canvas.setTextColor(C_DIM, C_BG);
    canvas.drawString(String("vol ") + (alertengine::masterVolume() * 100 / 255) + "%",
                       SCREEN_W - 44, y + 6);

    char ses[12];
    if (sightingstore::sdAvailable()) {
        snprintf(ses, sizeof(ses), "#%lu", static_cast<unsigned long>(sightingstore::sessionId()));
    } else {
        snprintf(ses, sizeof(ses), "--");
    }
    canvas.drawString(ses, SCREEN_W - 4, y + 6);
}

// Barras de señal, mismo criterio que shell-rc: -90dBm (nada) a -50dBm (llena).
void drawSignalBars(int x, int y, int rssi, uint16_t color)
{
    int bars = constrain((rssi + 90) / 10, 0, 4);
    for (int b = 0; b < 4; b++) {
        int h = 2 + b * 2;
        canvas.fillRect(x + b * 4, y + 9 - h, 3, h, b < bars ? color : C_OFF);
    }
}

// "Identificado" = tiene una categoría real de SignatureDB, no el default
// de "no matcheó nada". Separado de confidenceLow a propósito: un match
// no verificado sigue siendo un resultado identificado (sabemos qué es,
// aunque la firma no esté confirmada todavía), lo que no es identificado
// es Unknown -- ahí no sabemos nada.
bool isIdentified(signaturedb::Category c) { return c != signaturedb::Category::Unknown; }

// Orden de la lista, por prioridad: 1) sospechosos de convoy (la alerta
// más seria, salvo que sea tuyo -- ver `trusted`), 2) resultados
// identificados (sabemos qué son -- son los interesantes), 3) todo lo
// demás, 4) marcados como propios (ya los conocés, no urge mirarlos; un
// AirTag tuyo que vas a llevar siempre contigo calificaría para el
// grupo 1 si no fuera por esto). Dentro de cada grupo, por RSSI desc.
// Compartido entre drawList() y la selección por teclado -- ambos tienen
// que ver exactamente el mismo orden, si no cursorIndex apunta a una fila
// distinta de la que se ve.
std::vector<size_t> sortedOrder()
{
    size_t n = sightingstore::count();
    std::vector<size_t> order(n);
    for (size_t i = 0; i < n; i++) order[i] = i;
    std::sort(order.begin(), order.end(), [](size_t a, size_t b) {
        const sightingstore::Record *ra = sightingstore::byIndex(a);
        const sightingstore::Record *rb = sightingstore::byIndex(b);
        if (ra->trusted != rb->trusted) return !ra->trusted; // no-trusted primero
        bool convoyA = !ra->trusted && convoyanalyzer::isSuspect(ra->fingerprintHash);
        bool convoyB = !rb->trusted && convoyanalyzer::isSuspect(rb->fingerprintHash);
        if (convoyA != convoyB) return convoyA;
        bool idA = isIdentified(ra->category);
        bool idB = isIdentified(rb->category);
        if (idA != idB) return idA;
        return ra->lastRssi > rb->lastRssi;
    });
    return order;
}

void drawList()
{
    canvas.fillRect(0, LIST_TOP, SCREEN_W, SCREEN_H - LIST_TOP, C_BG);

    size_t n = sightingstore::count();

    if (n == 0) {
        canvas.setFont(&fonts::Font0);
        canvas.setTextDatum(middle_center);
        canvas.setTextColor(C_DIM, C_BG);
        canvas.drawString(blescanner::scanning() ? "Escaneando..." : "Scan detenido",
                           SCREEN_W / 2, LIST_TOP + (SCREEN_H - LIST_TOP) / 2);
        return;
    }

    std::vector<size_t> order = sortedOrder();
    bool selectable = !blescanner::scanning();
    bool scrollable = n > static_cast<size_t>(MAX_VISIBLE_ROWS);

    // scrollOffset sigue a cursorIndex: si la selección se fue arriba o
    // abajo de la ventana visible, la ventana se desliza para que entre.
    // Clampeamos con n real por si la lista encogió (no pasa hoy, pero
    // más vale no asumir).
    int maxOffset = std::max(0, static_cast<int>(n) - MAX_VISIBLE_ROWS);
    if (scrollOffset > maxOffset) scrollOffset = maxOffset;
    if (cursorIndex < scrollOffset) scrollOffset = cursorIndex;
    if (cursorIndex > scrollOffset + MAX_VISIBLE_ROWS - 1) scrollOffset = cursorIndex - MAX_VISIBLE_ROWS + 1;

    canvas.setFont(&fonts::Font0);
    size_t first = selectable ? static_cast<size_t>(scrollOffset) : 0;
    size_t shown = std::min<size_t>(order.size() - first, MAX_VISIBLE_ROWS);
    for (size_t i = 0; i < shown; i++) {
        size_t absIndex = first + i;
        const sightingstore::Record *r = sightingstore::byIndex(order[absIndex]);
        if (!r) continue;
        int y = LIST_TOP + static_cast<int>(i) * ROW_H;
        // Un dispositivo marcado como propio nunca se muestra como
        // sospechoso de convoy, aunque el score le dé igual -- ver
        // sortedOrder() y processSightings() para el por qué.
        bool convoySuspect = !r->trusted && convoyanalyzer::isSuspect(r->fingerprintHash);
        uint16_t color = r->trusted ? C_ON : categoryColor(r->category, r->confidenceLow);
        bool sel = selectable && static_cast<int>(absIndex) == cursorIndex;

        if (sel) canvas.fillRect(0, y, SCREEN_W, ROW_H - 1, C_HUD);

        // Franja de color a la izquierda: distingue la fila de un vistazo
        // sin tener que leer el label, igual que la cruz de shell-rc. Un
        // sospechoso de convoy pisa el color de categoría: es la alerta
        // más seria, no "este tipo de dispositivo".
        canvas.fillRect(0, y, 3, ROW_H - 1, convoySuspect ? C_ACCENT : color);

        uint16_t rowBg = sel ? C_HUD : C_BG;
        canvas.setTextDatum(middle_left);
        canvas.setTextColor(convoySuspect ? C_ACCENT : color, rowBg);
        const char *label = convoySuspect ? "!SIGUE" : (r->trusted ? "mio" : categoryLabel(r->category));
        canvas.drawString((sel ? "> " : "  ") + String(label), 6, y + ROW_H / 2);

        canvas.setTextColor(C_DIM, rowBg);
        canvas.setTextDatum(middle_right);
        canvas.drawString(r->lastMac.substring(9), 144, y + ROW_H / 2); // últimos 3 octetos
        if (r->confidenceLow) canvas.drawString("?", 152, y + ROW_H / 2);

        canvas.setTextColor(r->timesSeen > 1 ? C_TEXT : C_DIM, rowBg);
        canvas.drawString(String("x") + r->timesSeen, 180, y + ROW_H / 2);

        canvas.setTextDatum(middle_right);
        canvas.setTextColor(C_DIM, rowBg);
        canvas.drawString(String(r->lastRssi), SCREEN_W - 28, y + ROW_H / 2);
        drawSignalBars(SCREEN_W - 22, y + 1, r->lastRssi, color);
    }

    // Scrollbar: track + thumb en la franja angosta que liberamos a la
    // derecha. Solo se dibuja si de verdad hay más filas que lugar --
    // así no aparece sin necesidad en listas chicas.
    if (scrollable) {
        int trackX = SCREEN_W - 3;
        int trackH = SCREEN_H - LIST_TOP;
        canvas.drawFastVLine(trackX, LIST_TOP, trackH, C_HUD);
        int thumbH = std::max(4, trackH * MAX_VISIBLE_ROWS / static_cast<int>(n));
        int thumbY = LIST_TOP + trackH * scrollOffset / static_cast<int>(n);
        canvas.fillRect(trackX - 1, thumbY, 3, thumbH, C_ACCENT);
    }
}

void drawFooter(const char *text)
{
    canvas.setFont(&fonts::Font0);
    canvas.setTextDatum(bottom_center);
    canvas.setTextColor(C_DIM, C_BG);
    canvas.drawString(text, SCREEN_W / 2, SCREEN_H - 2);
}

// Pantalla de detalle: fabricante/tipo (si hay firma conocida) y todo lo
// que se puede inferir del advertising crudo -- RSSI, TX power declarado,
// tamaño del payload, nombre anunciado, rotaciones de MAC, score de
// convoy. No necesita el scan corriendo: es la última foto que tenemos
// del dispositivo seleccionado (el Record vive mientras dure la sesión).
void drawDetail()
{
    const sightingstore::Record *r = sightingstore::byFingerprint(selectedFingerprint);
    if (!r) {
        canvas.setFont(&fonts::Font0);
        canvas.setTextDatum(middle_center);
        canvas.setTextColor(C_DIM, C_BG);
        canvas.drawString("dispositivo perdido", SCREEN_W / 2, SCREEN_H / 2);
        return;
    }

    const signaturedb::Entry *entry = signaturedb::matchByCompanyId(r->lastCompanyId);

    canvas.setFont(&fonts::Font0);
    canvas.setTextDatum(middle_left);
    canvas.setTextColor(r->trusted ? C_ON : categoryColor(r->category, r->confidenceLow), C_BG);
    canvas.drawString(r->trusted ? String(categoryLabel(r->category)) + " [MIO]"
                                   : String(categoryLabel(r->category)),
                       4, 9);
    canvas.setTextDatum(middle_right);
    canvas.setTextColor(C_DIM, C_BG);
    canvas.drawString(r->lastMac, SCREEN_W - 4, 9);
    canvas.drawFastHLine(0, 17, SCREEN_W, C_HUD);

    int y = 24;
    constexpr int LINE_H = 11;

    auto row = [&](const char *label, const String &value, uint16_t valueColor) {
        canvas.setTextDatum(middle_left);
        canvas.setTextColor(C_DIM, C_BG);
        canvas.drawString(label, 4, y);
        canvas.setTextDatum(middle_right);
        canvas.setTextColor(valueColor, C_BG);
        canvas.drawString(value, SCREEN_W - 4, y);
        y += LINE_H;
    };

    String vendor = entry ? String(entry->vendorLabel)
                   : (r->category == signaturedb::Category::Headset ? String("heuristica (nombre)")
                                                                       : String("desconocido"));
    row("fabricante", vendor, (entry && !entry->verified) ? C_WARN : C_TEXT);

    char companyBuf[8];
    if (r->lastCompanyId == 0xFFFF) {
        strcpy(companyBuf, "--");
    } else {
        snprintf(companyBuf, sizeof(companyBuf), "0x%04X", r->lastCompanyId);
    }
    row("company id", companyBuf, C_TEXT);

    row("nombre anunciado", r->lastLocalName.length() ? r->lastLocalName : String("--"), C_TEXT);

    row("rssi actual", String(r->lastRssi) + " dBm", C_TEXT);

    row("tx power decl.",
        r->lastTxPowerDbm == INT8_MIN ? String("no declarado") : String(r->lastTxPowerDbm) + " dBm",
        C_TEXT);

    row("payload / visto", String(r->lastPayloadLen) + "b, x" + String(r->timesSeen), C_TEXT);

    uint32_t spanSec = (r->lastSeenMs - r->firstSeenMs) / 1000;
    row("ventana / rot. MAC", String(spanSec) + "s, " + String(r->macChanges) + "x",
        r->macChanges > 0 ? C_WARN : C_DIM);

    uint8_t convoyScore = convoyanalyzer::score(r->fingerprintHash);
    bool convoySuspect = !r->trusted && convoyanalyzer::isSuspect(r->fingerprintHash);
    row("score seg.", String(convoyScore) + "/100" + (r->trusted ? " (mio)" : ""),
        convoySuspect ? C_ACCENT : (r->trusted ? C_ON : C_DIM));

    if (r->confidenceLow) {
        canvas.setTextDatum(middle_center);
        canvas.setTextColor(C_WARN, C_BG);
        canvas.drawString("fingerprint de baja confianza (payload pobre)", SCREEN_W / 2, SCREEN_H - 14);
    }
}

// Pantalla de seguimiento: burbuja que crece/achica con la señal filtrada,
// estilo "Find Nearby" de Apple para AirTags, en vez de clasificar en
// estados discretos. Un radar implica dirección/posición que no tenemos;
// un texto de tendencia (v1/v2) resultó errático contra RSSI real (ver
// room_sweep.h) -- una magnitud continua no tiene estados que "parpadeen".
// El dBm crudo queda chico y abajo, solo para debug. Tono continuo opcional
// (toggle con G0 corto) para buscar sin mirar la pantalla.
void drawFollow()
{
    const sightingstore::Record *r = sightingstore::byFingerprint(roomsweep::followedHash());

    canvas.setFont(&fonts::Font0);
    canvas.setTextDatum(middle_left);
    canvas.setTextColor(C_ACCENT, C_BG);
    canvas.drawString("SIGUIENDO", 4, 8);
    canvas.setTextDatum(middle_right);
    canvas.setTextColor(C_DIM, C_BG);
    canvas.drawString(r ? categoryLabel(r->category) : "?", SCREEN_W - 4, 8);
    canvas.drawFastHLine(0, 16, SCREEN_W, C_HUD);

    const int cx = SCREEN_W / 2;
    const int cy = 68;
    const int maxR = 46;
    const int minR = 4; // nunca desaparece del todo -- "sin señal" sigue siendo un estado, no la nada

    // Radio mostrado: se desliza hacia el objetivo (maxR * fracción) en vez
    // de saltar de golpe en cada frame -- el filtro ya está en la EMA de
    // proximityFraction(), esto es solo para que la animación se vea fluida
    // a 60ms/frame en vez de temblar pixel a pixel. Se reinicia de golpe si
    // cambió el dispositivo seguido, para no animar desde una sesión vieja.
    static float displayR = minR;
    static uint64_t lastHash = 0;
    uint64_t hash = roomsweep::followedHash();
    if (hash != lastHash) {
        displayR = minR;
        lastHash = hash;
    }

    float frac = roomsweep::proximityFraction();
    float targetR = minR + frac * (maxR - minR);
    displayR += (targetR - displayR) * 0.25f;

    // Anillo exterior fijo = el "objetivo": cuando la burbuja interior lo
    // alcanza, estás tan cerca como el RSSI puede indicar.
    canvas.drawCircle(cx, cy, maxR, C_HUD);

    bool veryClose = frac > 0.85f;
    uint16_t bubbleColor = veryClose ? C_ON : C_ACCENT;
    int r = static_cast<int>(displayR + 0.5f);
    if (r < minR) r = minR;
    canvas.fillCircle(cx, cy, r, bubbleColor);

    // Lectura cruda chica abajo de la burbuja -- solo para debug, ya no es
    // la UI principal (ver room_sweep.h: el número solo no dice nada útil).
    canvas.setFont(&fonts::Font0);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(C_DIM, C_BG);
    canvas.drawString(String(roomsweep::smoothedRssi()) + " dBm", cx, 112);

    // Indicador de tono: toggle con G0 corto (no mantenido, eso es el
    // mute global). Mostramos el estado para que no quede como magia.
    canvas.setTextColor(roomsweep::toneMode() ? C_ON : C_DIM, C_BG);
    canvas.drawString(roomsweep::toneMode() ? "tono: ON (g0 apaga)" : "g0 corto: tono de busqueda",
                       cx, 124);
}

// En una zona con varios dispositivos, cada paquete de advertising (cada
// ~100-1000ms por dispositivo) marca `dirty`. Sin throttle, la lista se
// reordena por RSSI en cada uno de esos paquetes y el resultado es un
// parpadeo/"scroll" constante. Redibujamos como máximo a este ritmo en la
// lista; el flag `dirty` sigue en pie entre medio, así que no se pierde
// ningún cambio, solo se agrupan. El seguimiento necesita un ritmo más
// fino porque el sparkline se redibuja seguido, no solo cuando llega un
// avistamiento nuevo.
constexpr uint32_t LIST_RENDER_INTERVAL_MS = 300;
constexpr uint32_t FOLLOW_RENDER_INTERVAL_MS = 60;

void render()
{
    if (screen == Screen::Follow) dirty = true; // animación continua, no depende de nuevos avistamientos

    static uint32_t lastRenderMs = 0;
    uint32_t now = millis();
    uint32_t interval = screen == Screen::Follow ? FOLLOW_RENDER_INTERVAL_MS : LIST_RENDER_INTERVAL_MS;
    if (!dirty || (now - lastRenderMs < interval)) return;
    lastRenderMs = now;

    canvas.fillScreen(C_BG);

    if (screen == Screen::Follow) {
        drawFollow();
        drawFooter("del: volver");
        canvas.pushSprite(0, 0);
        dirty = false;
        return;
    }

    if (screen == Screen::Detail) {
        drawDetail();
        const sightingstore::Record *r = sightingstore::byFingerprint(selectedFingerprint);
        drawFooter((r && r->trusted) ? "enter: seguir   t: ya no es mio   del: volver"
                                      : "enter: seguir   t: marcar como mio   del: volver");
        canvas.pushSprite(0, 0);
        dirty = false;
        return;
    }

    drawHud();
    drawStatusBar();
    drawList();

    bool toastActive = toastMsg.length() > 0 && millis() < toastUntilMs;
    if (toastActive) {
        drawFooter(toastMsg.c_str());
    } else if (!blescanner::scanning() && sightingstore::count() > 0) {
        drawFooter("; . mover   enter: detalle   e: exportar   space: scan");
    } else {
        drawFooter("c: lugar   space: scan   +/- vol   G0: audio");
    }
    canvas.pushSprite(0, 0);
    dirty = false;
}

// --- Entrada -----------------------------------------------------------------------

bool hasChar(const Keyboard_Class::KeysState &ks, const char *chars)
{
    for (char c : ks.word) {
        if (strchr(chars, c)) return true;
    }
    return false;
}

void showToast(const String &msg, uint32_t durationMs = 2500)
{
    toastMsg = msg;
    toastUntilMs = millis() + durationMs;
    dirty = true;
}

void handleKeyboard()
{
    if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) return;
    auto ks = M5Cardputer.Keyboard.keysState();

    if (hasChar(ks, "+")) {
        alertengine::setMasterVolume(std::min(255, alertengine::masterVolume() + 20));
        dirty = true;
    }
    if (hasChar(ks, "-")) {
        alertengine::setMasterVolume(std::max(0, alertengine::masterVolume() - 20));
        dirty = true;
    }
    if (hasChar(ks, " ")) {
        if (blescanner::scanning()) blescanner::stop();
        else blescanner::start();
        dirty = true;
    }
    if (hasChar(ks, "c")) {
        convoyanalyzer::markNewContext();
        dirty = true;
    }
    if (hasChar(ks, "e")) {
        String base = exportmodule::exportNow();
        showToast(base.length() ? ("exportado: " + base) : "sin SD, no exporte");
    }

    if (screen == Screen::List) {
        // Selección solo con el scan pausado: la lista queda congelada
        // (nada nuevo llega de blescanner), así cursorIndex apunta siempre
        // a la misma fila mientras el usuario navega.
        if (!blescanner::scanning()) {
            size_t n = sightingstore::count();
            if (n > 0) {
                // Rango completo de la lista, no solo las filas visibles --
                // drawList() hace scroll para que cursorIndex siempre se vea.
                int maxIndex = static_cast<int>(n) - 1;
                if (cursorIndex > maxIndex) cursorIndex = maxIndex;
                if (cursorIndex < 0) cursorIndex = 0;
                if (hasChar(ks, ";w") && cursorIndex > 0) {
                    cursorIndex--;
                    dirty = true;
                }
                if (hasChar(ks, ".s") && cursorIndex < maxIndex) {
                    cursorIndex++;
                    dirty = true;
                }
                if (ks.enter) {
                    std::vector<size_t> order = sortedOrder();
                    if (cursorIndex < static_cast<int>(order.size())) {
                        const sightingstore::Record *r = sightingstore::byIndex(order[cursorIndex]);
                        if (r) {
                            selectedFingerprint = r->fingerprintHash;
                            screen = Screen::Detail;
                            dirty = true;
                        }
                    }
                }
            }
        }
    } else if (screen == Screen::Detail) {
        if (ks.enter) {
            roomsweep::follow(selectedFingerprint);
            screen = Screen::Follow;
            // El seguimiento necesita datos en vivo -- si el usuario pausó para
            // seleccionar, lo reanudamos acá.
            if (!blescanner::scanning()) blescanner::start();
            dirty = true;
        }
        if (hasChar(ks, "t")) {
            sightingstore::setTrusted(selectedFingerprint, !sightingstore::isTrusted(selectedFingerprint));
            dirty = true;
        }
        if (ks.del) {
            screen = Screen::List;
            dirty = true;
        }
    } else if (screen == Screen::Follow) {
        if (ks.del) {
            roomsweep::stop();
            screen = Screen::Detail; // se llegó al seguimiento desde Detail, se vuelve ahí
            dirty = true;
        }
    }
}

constexpr uint32_t G0_LONG_PRESS_MS = 800;

// G0 (BtnA) mantenido ~0.8s activa/desactiva el audio global. Arranca en
// mute (ver alert_engine.cpp) -- esto es la única forma de prenderlo, no
// hay tecla. G0 corto (soltado antes de llegar al mantenido), en la
// pantalla de seguimiento, activa/desactiva el tono continuo de búsqueda.
//
// No usamos un método wasReleased() de la librería porque en el resto del
// repo (karting, acá mismo) solo se probó isPressed()/pressedFor(), así
// que el flanco de bajada lo detectamos a mano comparando contra el
// estado del tick anterior -- menos elegante, pero no apuesta a una API
// que no está confirmada en este toolchain.
void handleG0()
{
    static bool longFired = false; // ya disparó la acción de mantenido en este apretón
    static bool wasDown = false;

    bool down = M5Cardputer.BtnA.isPressed();

    if (down) {
        if (!longFired && M5Cardputer.BtnA.pressedFor(G0_LONG_PRESS_MS)) {
            alertengine::setEnabled(!alertengine::enabled());
            longFired = true;
            dirty = true;
        }
    } else if (wasDown && !longFired && screen == Screen::Follow) {
        // Se soltó sin llegar al mantenido -- toque corto.
        roomsweep::setToneMode(!roomsweep::toneMode());
        dirty = true;
    }

    if (!down) longFired = false;
    wasDown = down;
}

void processSightings()
{
    static std::vector<blescanner::RawSighting> batch;
    batch.clear();
    if (blescanner::poll(batch) == 0) return;

    for (const auto &raw : batch) {
        fingerprint::AdvInput fin;
        fin.companyId = raw.companyId;
        fin.mfgTypeByte = raw.mfgTypeByte;
        fin.payloadLen = raw.payloadLen;
        fin.txPowerDbm = raw.txPowerDbm;
        fin.serviceUuids16 = raw.serviceUuids16;
        // adTypesPresentMask queda en 0 en v1: distinguir qué AD types trae
        // (más allá de companyId/serviceUuids, que ya entran al fingerprint
        // por su cuenta) es una mejora de precisión para una v2, no bloquea
        // el resto del pipeline.
        fingerprint::Result fp = fingerprint::compute(fin);

        // Mezclamos con lo que el Record ya sabía: muchos dispositivos
        // mandan el nombre recién en el scan response, no en todos los
        // paquetes, y si resolviéramos solo con el paquete de hoy,
        // "olvidaríamos" la identificación en cuanto llegue un paquete sin
        // nombre. Una vez que sabemos algo, no lo perdemos.
        const sightingstore::Record *existing = sightingstore::byFingerprint(fp.hash);
        uint16_t effectiveCompanyId = raw.companyId != 0xFFFF ? raw.companyId
                                     : (existing ? existing->lastCompanyId : 0xFFFF);
        uint8_t effectiveMfgTypeByte = raw.mfgTypeByte != 0 ? raw.mfgTypeByte
                                      : (existing ? existing->lastMfgTypeByte : 0);
        String effectiveLocalName = raw.localName.length() ? raw.localName
                                   : (existing ? existing->lastLocalName : String());

        Resolved res = resolve(effectiveCompanyId, effectiveMfgTypeByte, effectiveLocalName, fp.confidenceLow);

        sightingstore::SightingInput input;
        input.fingerprintHash = fp.hash;
        input.mac = raw.mac;
        input.rssi = raw.rssi;
        input.category = res.category;
        input.confidenceLow = res.confidenceLow;
        input.companyId = raw.companyId;
        input.mfgTypeByte = raw.mfgTypeByte;
        input.txPowerDbm = raw.txPowerDbm;
        input.payloadLen = raw.payloadLen;
        input.localName = raw.localName;

        sightingstore::Record rec;
        sightingstore::SightingKind kind = sightingstore::record(input, rec);

        // Marcado como propio ("es mío", ver Detail/tecla t) -- no alerta
        // ni cuenta como sospechoso de convoy. El caso típico es justo
        // esto: tu propio AirTag está siempre con vos, así que SIEMPRE
        // va a parecer que te sigue -- la única forma de que la app sea
        // útil contra rastreo ajeno es que vos le digas cuál es el tuyo.
        bool trusted = sightingstore::isTrusted(fp.hash);

        if (!trusted && (kind == sightingstore::SightingKind::New ||
                          kind == sightingstore::SightingKind::MacRotated)) {
            alertengine::trigger(res.alertProfile, fp.hash);
        }

        // Alimenta el ConvoyAnalyzer con todo, no solo New/MacRotated: lo
        // que le importa es "en qué contexto se vio este fingerprint", y
        // eso pasa en cada paquete, no solo cuando cambia de estado.
        convoyanalyzer::onSighting(fp.hash);
        if (!trusted && convoyanalyzer::isSuspect(fp.hash)) {
            alertengine::trigger(signaturedb::AlertProfile::ConvoySuspect, fp.hash);
        }

        // Si estamos siguiendo este fingerprint en la pantalla de
        // seguimiento, cada paquete (no solo New/Update) alimenta la
        // ventana corta de RoomSweep.
        roomsweep::sample(fp.hash, raw.rssi);

        dirty = true;
    }
}

} // namespace

void setup()
{
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setRotation(1);
    canvas.createSprite(SCREEN_W, SCREEN_H);
    initColors();

    sdOk = initSdCard();
    sightingstore::begin(sdOk);
    alertengine::begin();
    convoyanalyzer::begin();
    roomsweep::begin();
    exportmodule::begin(sdOk);
    blescanner::begin();
    blescanner::start();
}

void loop()
{
    M5Cardputer.update();
    handleKeyboard();
    handleG0();
    processSightings();
    alertengine::update();
    roomsweep::updateTone();
    render();
}
