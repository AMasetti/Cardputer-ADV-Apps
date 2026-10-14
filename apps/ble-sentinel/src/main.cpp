// BLE Sentinel: detector pasivo de dispositivos BLE por firma de
// manufacturer data. Ver docs/BLE_SENTINEL_SPEC.md para el diseño completo.
//
// v1 incluye: fingerprint resistente a rotación de MAC, SignatureDB con
// smart glasses (Meta/Luxottica, Snap), alertas de audio por categoría,
// log de avistamientos a SD (timestamp relativo al boot, sin RTC todavía),
// ConvoyAnalyzer (seguimiento sostenido entre "contextos" marcados a
// mano) y RoomSweep (seguir un dispositivo puntual con un radar de RSSI)
// + ExportModule (CSV/JSON con hash de integridad a SD).
// Pendiente para próximas versiones: WiFiSniffer, soporte de RTC por
// Grove, más firmas (drones, trackers).
//
// Flujo de pantallas: Lista -> (enter, con scan pausado) -> Detalle ->
// (enter) -> Radar. `del` siempre vuelve un paso atrás (Radar->Detalle,
// Detalle->Lista).
//
// Controles:
//   G0 (mantenido ~0.8s)  activar/silenciar alertas de audio (arranca en mute)
//   espacio               pausar/reanudar el scan
//   ; w  /  . s           (con el scan pausado) mover la selección en la lista
//   enter                 lista: abrir el detalle del seleccionado
//                         detalle: seguirlo con el radar
//   del                   volver un paso atrás (detalle <- radar, lista <- detalle)
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

enum class Screen : uint8_t { List, Detail, Radar };
Screen screen = Screen::List;
int cursorIndex = 0; // selección en la lista, solo usable con el scan pausado
uint64_t selectedFingerprint = 0; // dispositivo elegido, vive en Detail y Radar

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
        default: return "unknown";
    }
}

uint16_t categoryColor(signaturedb::Category c, bool confidenceLow)
{
    switch (c) {
        case signaturedb::Category::SmartGlasses:
        case signaturedb::Category::DroneRemoteId:
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

Resolved resolve(const blescanner::RawSighting &raw, bool fpConfidenceLow)
{
    if (const signaturedb::Entry *e = signaturedb::matchByCompanyId(raw.companyId)) {
        return {e->category, e->alertProfile, fpConfidenceLow || !e->verified};
    }
    if (signaturedb::matchHeadsetByName(raw.localName)) {
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

// Orden de la lista: primero los sospechosos de convoy (la alerta más
// seria), después por RSSI desc. Compartido entre drawList() y la
// selección por teclado -- ambos tienen que ver exactamente el mismo
// orden, si no cursorIndex apunta a una fila distinta de la que se ve.
std::vector<size_t> sortedOrder()
{
    size_t n = sightingstore::count();
    std::vector<size_t> order(n);
    for (size_t i = 0; i < n; i++) order[i] = i;
    std::sort(order.begin(), order.end(), [](size_t a, size_t b) {
        const sightingstore::Record *ra = sightingstore::byIndex(a);
        const sightingstore::Record *rb = sightingstore::byIndex(b);
        bool convoyA = convoyanalyzer::isSuspect(ra->fingerprintHash);
        bool convoyB = convoyanalyzer::isSuspect(rb->fingerprintHash);
        if (convoyA != convoyB) return convoyA;
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

    canvas.setFont(&fonts::Font0);
    size_t shown = std::min<size_t>(order.size(), MAX_VISIBLE_ROWS);
    for (size_t row = 0; row < shown; row++) {
        const sightingstore::Record *r = sightingstore::byIndex(order[row]);
        if (!r) continue;
        int y = LIST_TOP + row * ROW_H;
        bool convoySuspect = convoyanalyzer::isSuspect(r->fingerprintHash);
        uint16_t color = categoryColor(r->category, r->confidenceLow);
        bool sel = selectable && static_cast<int>(row) == cursorIndex;

        if (sel) canvas.fillRect(0, y, SCREEN_W, ROW_H - 1, C_HUD);

        // Franja de color a la izquierda: distingue la fila de un vistazo
        // sin tener que leer el label, igual que la cruz de shell-rc. Un
        // sospechoso de convoy pisa el color de categoría: es la alerta
        // más seria, no "este tipo de dispositivo".
        canvas.fillRect(0, y, 3, ROW_H - 1, convoySuspect ? C_ACCENT : color);

        uint16_t rowBg = sel ? C_HUD : C_BG;
        canvas.setTextDatum(middle_left);
        canvas.setTextColor(convoySuspect ? C_ACCENT : color, rowBg);
        canvas.drawString((sel ? "> " : "  ") + String(convoySuspect ? "!SIGUE" : categoryLabel(r->category)),
                           6, y + ROW_H / 2);

        canvas.setTextColor(C_DIM, rowBg);
        canvas.setTextDatum(middle_right);
        canvas.drawString(r->lastMac.substring(9), 148, y + ROW_H / 2); // últimos 3 octetos
        if (r->confidenceLow) canvas.drawString("?", 156, y + ROW_H / 2);

        canvas.setTextColor(r->timesSeen > 1 ? C_TEXT : C_DIM, rowBg);
        canvas.drawString(String("x") + r->timesSeen, 184, y + ROW_H / 2);

        canvas.setTextDatum(middle_right);
        canvas.setTextColor(C_DIM, rowBg);
        canvas.drawString(String(r->lastRssi), SCREEN_W - 24, y + ROW_H / 2);
        drawSignalBars(SCREEN_W - 18, y + 1, r->lastRssi, color);
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
    canvas.setTextColor(categoryColor(r->category, r->confidenceLow), C_BG);
    canvas.drawString(categoryLabel(r->category), 4, 9);
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
    bool convoySuspect = convoyanalyzer::isSuspect(r->fingerprintHash);
    row("score seguimiento", String(convoyScore) + "/100", convoySuspect ? C_ACCENT : C_DIM);

    if (r->confidenceLow) {
        canvas.setTextDatum(middle_center);
        canvas.setTextColor(C_WARN, C_BG);
        canvas.drawString("fingerprint de baja confianza (payload pobre)", SCREEN_W / 2, SCREEN_H - 14);
    }
}

// Pantalla de radar: seguir un dispositivo puntual vía RoomSweep. El
// ángulo del punto es FIJO por fingerprint (derivado del hash, no cambia
// nunca) -- lo único que varía con la señal es la distancia al centro.
// A propósito: no tenemos información de dirección real, y variar el
// ángulo además de la distancia sugeriría una posición que no podemos
// conocer. El barrido giratorio es cosmético (relojero visual de "radar
// en vivo"), no representa nada.
void drawRadar()
{
    const sightingstore::Record *r = sightingstore::byFingerprint(roomsweep::followedHash());

    canvas.setFont(&fonts::Font0);
    canvas.setTextDatum(middle_left);
    canvas.setTextColor(C_ACCENT, C_BG);
    canvas.drawString("SIGUIENDO", 4, 8);
    canvas.setTextDatum(middle_right);
    canvas.setTextColor(C_DIM, C_BG);
    canvas.drawString(r ? categoryLabel(r->category) : "?", SCREEN_W - 4, 8);

    const int cx = SCREEN_W / 2;
    const int cy = 66;
    const int maxR = 42;

    canvas.drawCircle(cx, cy, maxR, C_HUD);
    canvas.drawCircle(cx, cy, maxR * 2 / 3, C_HUD);
    canvas.drawCircle(cx, cy, maxR / 3, C_HUD);

    float sweepAngle = (millis() % 3000) / 3000.0f * 2.0f * static_cast<float>(M_PI);
    canvas.drawLine(cx, cy, cx + static_cast<int>(cosf(sweepAngle) * maxR),
                     cy + static_cast<int>(sinf(sweepAngle) * maxR), C_HUD);

    int8_t rssi = roomsweep::smoothedRssi();
    // -95dBm (débil) -> borde del radar; -40dBm (fuerte) -> centro.
    int rssiClamped = constrain(static_cast<int>(rssi), -95, -40);
    int dist = maxR - (rssiClamped + 95) * (maxR - 4) / 55;

    // Ángulo fijo derivado del hash: mismo dispositivo siempre en el mismo
    // "rayo" del radar, así la vista no salta aunque no sepamos su
    // dirección real.
    float fixedAngle = (roomsweep::followedHash() % 360) * (static_cast<float>(M_PI) / 180.0f);
    int bx = cx + static_cast<int>(cosf(fixedAngle) * dist);
    int by = cy + static_cast<int>(sinf(fixedAngle) * dist);
    canvas.fillCircle(bx, by, 4, C_ACCENT);

    canvas.setTextDatum(middle_center);
    canvas.setTextColor(C_TEXT, C_BG);
    canvas.drawString(String(rssi) + " dBm", cx, cy + maxR + 12);

    const char *trendStr = roomsweep::trend() == roomsweep::Trend::Closer ? "acercandose"
                          : roomsweep::trend() == roomsweep::Trend::Farther ? "alejandose"
                                                                             : "estable";
    canvas.setTextColor(C_DIM, C_BG);
    canvas.drawString(trendStr, cx, cy + maxR + 23);

    if (r) {
        canvas.setTextDatum(bottom_left);
        canvas.setTextColor(C_DIM, C_BG);
        canvas.drawString(r->lastMac, 4, SCREEN_H - 14);
    }

    canvas.setTextDatum(bottom_center);
    canvas.setTextColor(C_WARN, C_BG);
    canvas.drawString("senial relativa, no posicion real", cx, SCREEN_H - 14);
}

// En una zona con varios dispositivos, cada paquete de advertising (cada
// ~100-1000ms por dispositivo) marca `dirty`. Sin throttle, la lista se
// reordena por RSSI en cada uno de esos paquetes y el resultado es un
// parpadeo/"scroll" constante. Redibujamos como máximo a este ritmo en la
// lista; el flag `dirty` sigue en pie entre medio, así que no se pierde
// ningún cambio, solo se agrupan. El radar necesita un ritmo más fino
// porque el barrido giratorio es una animación continua, no solo una
// reacción a datos nuevos.
constexpr uint32_t LIST_RENDER_INTERVAL_MS = 300;
constexpr uint32_t RADAR_RENDER_INTERVAL_MS = 60;

void render()
{
    if (screen == Screen::Radar) dirty = true; // animación continua, no depende de nuevos avistamientos

    static uint32_t lastRenderMs = 0;
    uint32_t now = millis();
    uint32_t interval = screen == Screen::Radar ? RADAR_RENDER_INTERVAL_MS : LIST_RENDER_INTERVAL_MS;
    if (!dirty || (now - lastRenderMs < interval)) return;
    lastRenderMs = now;

    canvas.fillScreen(C_BG);

    if (screen == Screen::Radar) {
        drawRadar();
        drawFooter("del: volver");
        canvas.pushSprite(0, 0);
        dirty = false;
        return;
    }

    if (screen == Screen::Detail) {
        drawDetail();
        drawFooter("enter: radar   del: volver");
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
                int maxIndex = static_cast<int>(std::min(n, static_cast<size_t>(MAX_VISIBLE_ROWS))) - 1;
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
            screen = Screen::Radar;
            // El radar necesita datos en vivo -- si el usuario pausó para
            // seleccionar, lo reanudamos acá.
            if (!blescanner::scanning()) blescanner::start();
            dirty = true;
        }
        if (ks.del) {
            screen = Screen::List;
            dirty = true;
        }
    } else if (screen == Screen::Radar) {
        if (ks.del) {
            roomsweep::stop();
            screen = Screen::Detail; // se llegó al radar desde Detail, se vuelve ahí
            dirty = true;
        }
    }
}

constexpr uint32_t G0_LONG_PRESS_MS = 800;

// G0 (BtnA) mantenido activa/desactiva el audio. Arranca en mute (ver
// alert_engine.cpp) -- esto es la única forma de prenderlo, no hay tecla.
// `handled` evita que se togglee varias veces mientras el botón sigue
// apretado: un solo toggle por mantenido, igual que el patrón de `karting`.
void handleG0()
{
    static bool handled = false;
    if (M5Cardputer.BtnA.isPressed()) {
        if (!handled && M5Cardputer.BtnA.pressedFor(G0_LONG_PRESS_MS)) {
            alertengine::setEnabled(!alertengine::enabled());
            handled = true;
            dirty = true;
        }
    } else {
        handled = false;
    }
}

void processSightings()
{
    static std::vector<blescanner::RawSighting> batch;
    batch.clear();
    if (blescanner::poll(batch) == 0) return;

    for (const auto &raw : batch) {
        fingerprint::AdvInput fin;
        fin.companyId = raw.companyId;
        fin.payloadLen = raw.payloadLen;
        fin.txPowerDbm = raw.txPowerDbm;
        fin.serviceUuids16 = raw.serviceUuids16;
        // adTypesPresentMask queda en 0 en v1: distinguir qué AD types trae
        // (más allá de companyId/serviceUuids, que ya entran al fingerprint
        // por su cuenta) es una mejora de precisión para una v2, no bloquea
        // el resto del pipeline.
        fingerprint::Result fp = fingerprint::compute(fin);

        Resolved res = resolve(raw, fp.confidenceLow);

        sightingstore::SightingInput input;
        input.fingerprintHash = fp.hash;
        input.mac = raw.mac;
        input.rssi = raw.rssi;
        input.category = res.category;
        input.confidenceLow = res.confidenceLow;
        input.companyId = raw.companyId;
        input.txPowerDbm = raw.txPowerDbm;
        input.payloadLen = raw.payloadLen;
        input.localName = raw.localName;

        sightingstore::Record rec;
        sightingstore::SightingKind kind = sightingstore::record(input, rec);

        if (kind == sightingstore::SightingKind::New ||
            kind == sightingstore::SightingKind::MacRotated) {
            alertengine::trigger(res.alertProfile, fp.hash);
        }

        // Alimenta el ConvoyAnalyzer con todo, no solo New/MacRotated: lo
        // que le importa es "en qué contexto se vio este fingerprint", y
        // eso pasa en cada paquete, no solo cuando cambia de estado.
        convoyanalyzer::onSighting(fp.hash);
        if (convoyanalyzer::isSuspect(fp.hash)) {
            alertengine::trigger(signaturedb::AlertProfile::ConvoySuspect, fp.hash);
        }

        // Si estamos siguiendo este fingerprint en el radar, cada paquete
        // (no solo New/Update) alimenta la ventana corta de RoomSweep.
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
    render();
}
