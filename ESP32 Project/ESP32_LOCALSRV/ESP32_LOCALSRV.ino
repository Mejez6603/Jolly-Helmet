// =====================================================================
//  LOCAL SERVER - the technician's CONTROL CENTER                         (ESP32, core 3.3.7)
//  Rewritten 2026-10-03 for the deployed system: Box 1 (S3A/A1/A2/A3), Box 2 (S3C/C1/C2), Box 3 (S3B/B1/B2/B3), ACS 1 and ACS 2.
//
//  What it is: a Wi-Fi access point ("JollyHelmet-Local" / "12345678") with a captive portal. Open http://192.168.4.1 on a phone:
//    /       home - who is online, what each box is doing, revenue statistics
//    /box1   Box 1 settings + calibration + maintenance      /box2   Box 2      /box3   Box 3      /acs   ACS 1 and ACS 2
//  The boxes run entirely on their own (each S3 node owns its box's state machine and keeps its settings in its own flash); this
//  node only LISTENS, shows, and sends settings / maintenance commands - so it can be switched off, swapped or reflashed at any
//  time without the machines stopping or forgetting a setting. The only things it stores itself are the revenue statistics.
//  See settings_hub.h for the page API. The old A2-relay / Box-1-tab / 4-tank-ACS dashboard was removed on purpose: its Settings
//  tab pushed the OLD settings list into the new nodes.
// =====================================================================
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <DNSServer.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <esp_heap_caps.h>

#include "Shared_Common.h"
#include "Restate_Common.h"
static_assert(sizeof(CoinBookPacket) != sizeof(RestatePacket), "CoinBookPacket and RestatePacket are the same size - processPacket() dispatch-by-length would misroute them");
#include "index_html.h"
#include "style_css.h"
#include "script_js.h"

// The main loop() does all the radio work and builds the status JSON: give it a roomy stack (default 8 KB).
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

// Access Point Configurations
const char* AP_SSID     = "JollyHelmet-Local";
const char* AP_PASSWORD = "12345678";
const byte  DNS_PORT    = 53;

DNSServer      dnsServer;
AsyncWebServer server(80);
Preferences    prefs;

unsigned long lastOneSecTick = 0;

// CommandPacket carries no "who sent this" field, only "who it's addressed to". The node MACs are learned from each node's own
// broadcasts (which carry a deviceID) so commands to S3A / S3C / A1 / C1 can go out as unicast (more reliable than broadcast).
// Indexed by DeviceID; ids >= 8 (Box 3's nodes, ACS 2) are simply broadcast to.
uint8_t knownMac[8][6] = {0};
bool    macKnown[8]    = {false};

// -------------------------------------------------------------
// Statistics & revenue (persisted to NVS) - the only state this node owns
// -------------------------------------------------------------
float coinValuePeso = 1.0f; // PHP per pulse
#define STAT_HISTORY_SIZE 10

struct SessionRecord {
    uint16_t durationSec;
    uint16_t coinsUsed;
    uint8_t  cancelled;       // 1 = the customer aborted mid-cycle, 0 = the cycle ran to the end
};
SessionRecord statHistory[STAT_HISTORY_SIZE];
uint8_t  statHistoryCount = 0;
uint8_t  statHistoryHead  = 0;

uint32_t statTotalSessions    = 0;
uint32_t statTotalCoins       = 0;
uint32_t statTotalDurationSec = 0;

// Finished and cancelled sessions (2026-10-13). A session that ran to the end is "finished"; one the customer aborted mid-cycle is "cancelled"
// (its coins are still revenue). Both count as a user served in the totals above. These six counters only hold the sessions recorded since this
// firmware, the totals above also hold everything before it.
uint32_t statFinSessions = 0, statFinCoins = 0, statFinDurationSec = 0;
uint32_t statCanSessions = 0, statCanCoins = 0, statCanDurationSec = 0;

// Restated old data (2026-10-11): the spare ESP32_RESTATE board broadcasts the old totals restated at the new price list; they are kept here
// (flash) and shown NEXT TO the totals above. The totals above are never changed by it.
RestatePacket rsPkt;
bool          rsHave = false;
RestateAdj    adj;                 // after a mode-1 "apply": the ORIGINAL figures at the capture and what the old coins were corrected to
uint16_t      adjIgnoreRev = 0;    // after "Restore original" the repeats of that same update must not be applied again
uint8_t       adjWithdrawLeft = 0; // after "Restore original": tell the Online Server (3 times) to drop its "statistics corrected" note

// Revenue is tallied from each coin terminal's own pulse counter (A1 = Box 1, C1 = Box 2, B1 = Box 3). The last count seen is kept in
// NVS so a reboot here does not double-count pulses already tallied (this exact bug happened once).
// SINCE 2026-10-13 Box 1 and Box 3 are booked differently (see lastBooked below): once their S3 sends booked coins, the terminal's pulse
// counter stops adding to the revenue, because it also counted electrical noise and the coins of cancelled payments. It is still tracked.
uint32_t lastKnownPulses[3] = {0, 0, 0};
const char* const PULSE_KEYS[3]  = {"last_a1_pulses", "last_c1_pulses", "last_b1_pulses"};
const char* const PULSE_NAMES[3] = {"A1", "C1", "B1"};

// Booked coins (2026-10-13): Box 1's S3A and Box 3's S3B send the running total of the coins THEY accepted for paid sessions (CoinBookPacket,
// every second). The revenue books the difference to the last total seen (kept in NVS, so a reboot here never double-counts). A box that has
// sent it once stays on this way of counting (bookedOn, kept in NVS). Box 2 sends none: its revenue still comes from C1's pulse counter.
// Index like the pulses: 0 = Box 1 (S3A), 1 = Box 2 (unused), 2 = Box 3 (S3B).
uint32_t lastBooked[3] = {0, 0, 0};
bool     bookedOn[3]   = {false, false, false};
const char* const BOOK_KEYS[3]    = {"bk_s3a", "bk_s3c", "bk_s3b"};
const char* const BOOK_ON_KEYS[3] = {"bkon_s3a", "bkon_s3c", "bkon_s3b"};
const char* const BOOK_NAMES[3]   = {"S3A (Box 1)", "S3C (Box 2)", "S3B (Box 3)"};

// Completed-session tracking: each S3 node's completedSessionSeq changes once per finished session. The first packet ever seen only
// sets the starting point (otherwise the node's whole history would be replayed as one phantom session).
uint32_t lastKnownSessionSeq[3] = {0, 0, 0};   // 0 = S3A (Box 1), 1 = S3C (Box 2), 2 = S3B (Box 3)
bool     sessionSeqInitialized[3] = {false, false, false};

// Forward declaration - settings_hub.h calls this; it is defined further down. (An explicit prototype is needed because of the default
// argument: the IDE's auto-prototype generator does not handle that case reliably.)
void sendEspNowCmdDirect(uint8_t targetDev, uint8_t cmdId, uint8_t subIdx, uint16_t param, uint8_t state, const char* str = nullptr);

void loadSettingsFromNVS() {
    prefs.begin("vending_nvs", false);

    statTotalSessions    = prefs.getUInt("st_sess", 0);
    statTotalCoins       = prefs.getUInt("st_coins", 0);
    statTotalDurationSec = prefs.getUInt("st_dur", 0);
    coinValuePeso        = prefs.getFloat("coin_val", 1.0f);
    for (uint8_t i = 0; i < 3; i++) lastKnownPulses[i] = prefs.getUInt(PULSE_KEYS[i], 0);
    for (uint8_t i = 0; i < 3; i++) { lastBooked[i] = prefs.getUInt(BOOK_KEYS[i], 0); bookedOn[i] = prefs.getBool(BOOK_ON_KEYS[i], false); }
    statFinSessions = prefs.getUInt("fs_n", 0); statFinCoins = prefs.getUInt("fs_c", 0); statFinDurationSec = prefs.getUInt("fs_d", 0);
    statCanSessions = prefs.getUInt("cs_n", 0); statCanCoins = prefs.getUInt("cs_c", 0); statCanDurationSec = prefs.getUInt("cs_d", 0);
    memset(&adj, 0, sizeof(adj));
    if (prefs.getBytesLength("adj_rec") == sizeof(adj)) prefs.getBytes("adj_rec", &adj, sizeof(adj));
    adjIgnoreRev = prefs.getUShort("adj_ign", 0);
    memset(&rsPkt, 0, sizeof(rsPkt));
    if (prefs.getBytesLength("rs_pkt") == sizeof(rsPkt)) {
        prefs.getBytes("rs_pkt", &rsPkt, sizeof(rsPkt));
        rsHave = (rsPkt.magic == RESTATE_MAGIC && rsPkt.active == 1);
    }

    Serial.println("[MASTER NVS] Statistics loaded.");
}

// -------------------------------------------------------------
// Core 3.3.7 ESP-NOW Direct Command Dispatcher
// -------------------------------------------------------------
void onDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
    if (!tx_info) return;
}

// Registers a specific peer MAC for unicast sending, if it isn't already registered.
// Broadcast doesn't need this - BROADCAST_MAC is implicitly always sendable.
void ensureUnicastPeer(const uint8_t* peerMac) {
    if (esp_now_is_peer_exist(peerMac)) return;
    esp_now_peer_info_t peerInfo;
    memset(&peerInfo, 0, sizeof(esp_now_peer_info_t));
    memcpy(peerInfo.peer_addr, peerMac, 6);
    peerInfo.channel = ESPNOW_WIFI_CHANNEL;
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);
}

void sendEspNowCmdDirect(uint8_t targetDev, uint8_t cmdId, uint8_t subIdx, uint16_t param, uint8_t state, const char* str) {
    CommandPacket cmd;
    memset(&cmd, 0, sizeof(CommandPacket));
    cmd.targetDeviceID = targetDev;
    cmd.commandID      = cmdId;
    cmd.subIndex       = subIdx;
    cmd.param16        = param;
    cmd.state          = state;
    if (str != nullptr) {
        strncpy(cmd.payloadStr, str, sizeof(cmd.payloadStr) - 1);
        cmd.payloadStr[sizeof(cmd.payloadStr) - 1] = '\0';
    }

    // Unicast once the target's MAC is known - only that node's radio has to process it. Falls back to broadcast until a MAC is learned.
    bool haveUnicast = (targetDev < 8) && macKnown[targetDev];
    const uint8_t* destMac = haveUnicast ? knownMac[targetDev] : BROADCAST_MAC;
    if (haveUnicast) ensureUnicastPeer(destMac);

    Serial.printf("[MASTER ESP-NOW TX] opcode %d -> device %d (%s) (sub %d, param %d, state %d)\n",
                  cmdId, targetDev, haveUnicast ? "unicast" : "broadcast", subIdx, param, state);

    esp_err_t result = esp_now_send(destMac, (uint8_t*)&cmd, sizeof(CommandPacket));
    if (result != ESP_OK) {
        Serial.printf("[MASTER ESP-NOW TX] ERROR: send failed with code %d\n", result);
    }
}

void learnMac(uint8_t deviceId, const uint8_t* mac) {
    if (deviceId >= 8 || macKnown[deviceId]) return; // learn once - a node's MAC never changes
    memcpy(knownMac[deviceId], mac, 6);
    macKnown[deviceId] = true;
    Serial.printf("[MASTER] Learned MAC for device %d: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  deviceId, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

// Records what an S3 node reported about the session it just ended (one combined revenue dashboard for all three boxes). Since 2026-10-13 the
// coin field carries the session's coins in its low 15 bits and SESSION_CANCELLED_FLAG when the customer aborted mid-cycle; an S3 on older
// firmware never sets the flag, so its sessions simply show as finished. The coins here only feed the finished / cancelled statistics and
// the last-10 chart - the revenue itself is booked from the S3's booked-coin total (trackBooked) or, for Box 2, from the terminal's pulses.
void logAppendMachine(uint8_t boxNo, bool cancelled, uint16_t coins, uint16_t durSec);      // session_log.h: the permanent, timestamped log
void recordCompletedSession(uint8_t boxNo, uint16_t durSec16, uint16_t coins16) {
    bool cancelled = (coins16 & SESSION_CANCELLED_FLAG) != 0;
    uint16_t coins = coins16 & (uint16_t)~SESSION_CANCELLED_FLAG;
    statTotalSessions++;
    statTotalDurationSec += durSec16;
    if (cancelled) {
        statCanSessions++; statCanCoins += coins; statCanDurationSec += durSec16;
        prefs.putUInt("cs_n", statCanSessions); prefs.putUInt("cs_c", statCanCoins); prefs.putUInt("cs_d", statCanDurationSec);
    } else {
        statFinSessions++; statFinCoins += coins; statFinDurationSec += durSec16;
        prefs.putUInt("fs_n", statFinSessions); prefs.putUInt("fs_c", statFinCoins); prefs.putUInt("fs_d", statFinDurationSec);
    }
    statHistory[statHistoryHead] = { durSec16, coins, (uint8_t)(cancelled ? 1 : 0) };
    statHistoryHead = (statHistoryHead + 1) % STAT_HISTORY_SIZE;
    if (statHistoryCount < STAT_HISTORY_SIZE) statHistoryCount++;

    prefs.putUInt("st_sess", statTotalSessions);
    prefs.putUInt("st_dur", statTotalDurationSec);
    Serial.printf("[MASTER STATS] Session #%d recorded (%s): %ds, %d coins.\n", statTotalSessions, cancelled ? "CANCELLED" : "finished", durSec16, coins);
    logAppendMachine(boxNo, cancelled, coins, durSec16);               // and into the permanent timestamped log
}

// After a backup CSV was restored (session_log.h): the Home page totals are raised to at least what the rebuilt log says, so a board that came back at 0 shows its
// history again. They are never lowered (sessions from before the log existed, and revenue booked outside it, stay counted).
void applyRestoredTotals(uint32_t finN, uint32_t finCoins, uint32_t finDur, uint32_t canN, uint32_t canCoins, uint32_t canDur) {
    if (statFinSessions < finN) statFinSessions = finN;
    if (statFinCoins < finCoins) statFinCoins = finCoins;
    if (statFinDurationSec < finDur) statFinDurationSec = finDur;
    if (statCanSessions < canN) statCanSessions = canN;
    if (statCanCoins < canCoins) statCanCoins = canCoins;
    if (statCanDurationSec < canDur) statCanDurationSec = canDur;
    if (statTotalSessions < finN + canN) statTotalSessions = finN + canN;
    if (statTotalCoins < finCoins + canCoins) statTotalCoins = finCoins + canCoins;
    if (statTotalDurationSec < finDur + canDur) statTotalDurationSec = finDur + canDur;
    prefs.putUInt("fs_n", statFinSessions); prefs.putUInt("fs_c", statFinCoins); prefs.putUInt("fs_d", statFinDurationSec);
    prefs.putUInt("cs_n", statCanSessions); prefs.putUInt("cs_c", statCanCoins); prefs.putUInt("cs_d", statCanDurationSec);
    prefs.putUInt("st_sess", statTotalSessions); prefs.putUInt("st_coins", statTotalCoins); prefs.putUInt("st_dur", statTotalDurationSec);
}

// A box's booked-coin total (Box 1 = 0, Box 3 = 2). The first packet a box ever sends only sets the starting point: what it had booked before
// was already counted from its pulses. From then on every coin it books is revenue the moment the total goes up.
void trackBooked(uint8_t i, uint32_t total) {
    if (!bookedOn[i]) {
        bookedOn[i] = true;
        lastBooked[i] = total;
        prefs.putBool(BOOK_ON_KEYS[i], true);
        prefs.putUInt(BOOK_KEYS[i], total);
        Serial.printf("[MASTER STATS] %s now books its own coins (starting at %lu). Its terminal's raw pulses no longer count as revenue.\n", BOOK_NAMES[i], (unsigned long)total);
        return;
    }
    if (total > lastBooked[i]) {
        uint32_t n = total - lastBooked[i];
        lastBooked[i] = total;
        prefs.putUInt(BOOK_KEYS[i], total);
        statTotalCoins += n;
        prefs.putUInt("st_coins", statTotalCoins);
        Serial.printf("[MASTER STATS] +%lu coin(s) booked by %s.\n", (unsigned long)n, BOOK_NAMES[i]);
    } else if (total < lastBooked[i]) {
        // the box's own memory was cleared or replaced (its total restarted): move the baseline down, never subtract revenue
        Serial.printf("[MASTER STATS] %s booked total dropped (%lu -> %lu) - it was reset, resyncing.\n", BOOK_NAMES[i], (unsigned long)lastBooked[i], (unsigned long)total);
        lastBooked[i] = total;
        prefs.putUInt(BOOK_KEYS[i], total);
    }
}

void trackCoins(uint8_t i, uint32_t pulseCount) {
    if (pulseCount > lastKnownPulses[i]) {
        uint32_t newPulses = pulseCount - lastKnownPulses[i];
        lastKnownPulses[i] = pulseCount;
        prefs.putUInt(PULSE_KEYS[i], pulseCount);
        if (bookedOn[i]) {
            Serial.printf("[MASTER STATS] %s: %u pulse(s) seen at the terminal (the box books its own coins, nothing added here).\n", PULSE_NAMES[i], (unsigned)newPulses);
        } else {
            statTotalCoins += newPulses;
            prefs.putUInt("st_coins", statTotalCoins);
            Serial.printf("[MASTER STATS] +%u coin(s) from %s.\n", (unsigned)newPulses, PULSE_NAMES[i]);
        }
    } else if (pulseCount < lastKnownPulses[i]) {
        // the terminal itself rebooted (its own counter restarted) - move the baseline down instead of double-counting or freezing
        Serial.printf("[MASTER STATS] %s pulse count dropped (%u -> %u) - it likely rebooted, resyncing.\n", PULSE_NAMES[i], (unsigned)lastKnownPulses[i], (unsigned)pulseCount);
        lastKnownPulses[i] = pulseCount;
        prefs.putUInt(PULSE_KEYS[i], pulseCount);
    }
}

void trackSession(uint8_t i, const BoxStatusPacket& s) {
    if (!sessionSeqInitialized[i]) {
        lastKnownSessionSeq[i] = s.completedSessionSeq;
        sessionSeqInitialized[i] = true;
    } else if (s.completedSessionSeq != lastKnownSessionSeq[i]) {
        // The S3's session counter starts at 0 again every time it restarts (power-up, reflash). A counter that went DOWN is that restart, not a
        // new session - recording it would add a phantom "0 s, 0 coins" user served every time a box reboots.
        bool restarted = s.completedSessionSeq < lastKnownSessionSeq[i];
        lastKnownSessionSeq[i] = s.completedSessionSeq;
        if (restarted) Serial.println("[MASTER STATS] A box restarted (its session counter went back) - no session recorded.");
        else recordCompletedSession((uint8_t)(i + 1), s.lastSessionDurationSec, s.lastSessionCoins);      // i: 0 = Box 1, 1 = Box 2, 2 = Box 3
    }
}

// A RestatePacket from the spare ESP32_RESTATE board (its Update button). Stored only when it is new (another rev, or withdrawn/active changed).
void restateOnPacket(const uint8_t* d) {
    RestatePacket p;
    memcpy(&p, d, sizeof(p));
    if (p.deviceID != DEVICE_SERVER || p.magic != RESTATE_MAGIC) return;
    if (p.mode == 1) {                                         // APPLY: replace the coin total with the corrected one (the original is archived)
        if (p.active != 1) return;
        if (adj.on) {                                          // already applied: the same update repeats every 10 s; a different one needs a restore first
            if (p.rev != adj.rev) Serial.println("[MASTER RESTATE] A different update arrived but one is already applied: press 'Restore original' on the Home page first.");
            return;
        }
        if (p.rev == adjIgnoreRev) return;                     // restored on purpose: do not apply its repeats again
        if (statTotalCoins < p.baseCoins || statTotalSessions < p.baseSessions) {
            Serial.println("[MASTER RESTATE] Not applied: the statistics were reset after the capture.");
            return;
        }
        uint32_t sales = statTotalCoins - p.baseCoins;         // coins sold since the capture stay as they are (they were sold at the new list)
        adj.on = 1; adj.rev = p.rev; adj.tzMin = p.tzMin; adj.cvX100 = p.cvX100; adj.capturedAt = p.capturedAt;
        adj.baseSessions = p.baseSessions; adj.baseCoins = p.baseCoins; adj.restCoins = p.oldRestCoins;
        prefs.putBytes("adj_rec", &adj, sizeof(adj));          // the record first, then the new total
        statTotalCoins = p.oldRestCoins + sales;
        prefs.putUInt("st_coins", statTotalCoins);
        Serial.printf("[MASTER RESTATE] Applied update #%u: old data %lu coins corrected to %lu coins (original kept, restore on the Home page).\n", (unsigned)p.rev, (unsigned long)p.baseCoins, (unsigned long)p.oldRestCoins);
        return;
    }
    if (p.active != 1) {                                       // withdrawn
        if (!rsHave) return;
        rsHave = false;
        memset(&rsPkt, 0, sizeof(rsPkt));
        prefs.remove("rs_pkt");
        Serial.println("[MASTER RESTATE] Restated data withdrawn.");
        return;
    }
    if (rsHave && p.rev == rsPkt.rev) return;                  // the same update again (it is repeated every 10 s)
    rsPkt = p;
    rsHave = true;
    prefs.putBytes("rs_pkt", &rsPkt, sizeof(rsPkt));
    Serial.printf("[MASTER RESTATE] Update #%u stored: old data %lu coins restated to %lu coins.\n", (unsigned)p.rev, (unsigned long)p.baseCoins, (unsigned long)p.oldRestCoins);
}

// Revenue totals for the Online Server (2026-10-09): the same numbers the Home page shows, broadcast every 10 s so the Online Server needs
// no counting of its own. Nothing else listens to this packet (its size is unique, see Shared_Common.h).
uint16_t hubStatsSeq = 0;
void broadcastHubStats() {
    HubStatsPacket p;
    memset(&p, 0, sizeof(p));
    p.deviceID         = DEVICE_SERVER;
    int cv             = (int)(coinValuePeso * 100.0f + 0.5f);
    p.coinValueX100    = (uint16_t)(cv < 0 ? 0 : (cv > 65535 ? 65535 : cv));
    p.totalSessions    = statTotalSessions;
    p.totalCoins       = statTotalCoins;
    p.totalDurationSec = statTotalDurationSec;
    p.seq              = ++hubStatsSeq;
    p.histCount        = statHistoryCount;
    for (uint8_t i = 0; i < statHistoryCount && i < 10; i++) {
        uint8_t idx = (statHistoryHead + STAT_HISTORY_SIZE - statHistoryCount + i) % STAT_HISTORY_SIZE;   // oldest first
        p.histDur[i]   = statHistory[idx].durationSec;
        p.histCoins[i] = statHistory[idx].coinsUsed;
    }
    esp_now_send(BROADCAST_MAC, (uint8_t*)&p, sizeof(p));
}

// The permanent session log and the clock (session_log.h) - after the statistics and the ESP-NOW helpers it uses.
#include "session_log.h"

// -------------------------------------------------------------
// Crash report. RTC memory survives a watchdog / panic restart (a power cut clears it), so after an unexpected restart the board can say what it was
// doing just before: the last pages the browser asked for, the free memory, and where loop() was. Printed at start-up and shown on the Home page.
// -------------------------------------------------------------
#define CRUMB_REQ 8
#define CRUMB_MAGIC 0xC8A5B009UL
struct CrumbRec {
    uint32_t magic;
    uint32_t upSec, freeHeap, minHeap, largest;       // the last once-a-second sample
    uint32_t epoch;                                   // ... and the clock (unix time) at that sample, 0 = the clock was not set
    uint8_t  stage, head;                             // stage: where loop() was (1 radio queue, 2 DNS, 3 sockets, 4 tick, 5 log, 6 stats, 7 JSON, 8 send)
    uint32_t at[CRUMB_REQ];                           // seconds since power-on of each request
    char     what[CRUMB_REQ][40];                     // "GET /box1" ... (oldest is the one after head)
    uint32_t okUp, badUp;                             // heap watch: when the last clean check was, when the first damaged one was (0 = none)
    char     okAt[24], badAt[24];                     // ... and where in the program they were
};
static RTC_NOINIT_ATTR CrumbRec crumb;
static String crashInfo;                              // the report of the LAST unexpected restart (empty after a normal start)

// HEAP WATCH. The memory manager keeps its bookkeeping next to the data; a write a few bytes past the end of a buffer (or into freed memory) damages it, and the
// crash only comes much later, when something else is freed (the "prev block is not free" crash). This checks the bookkeeping at several places, remembers the
// FIRST place where it was found damaged and prints it at once: the program part that ran between the last clean check and that one is the one to look at.
static bool heapDamaged = false;
static char heapLiveNote[300] = "";                   // the same finding as text, shown on the Home page at once (the board may keep running for a while after damage was found)
static void heapWatch(const char* where);
static String crumbRequests();

static void crumbNote(const char* method, const char* url) {
    uint8_t h = crumb.head % CRUMB_REQ;
    char* w = crumb.what[h];
    size_t n = (size_t)snprintf(w, sizeof(crumb.what[h]), "%s %s", method, url);
    if (n >= sizeof(crumb.what[h])) n = sizeof(crumb.what[h]) - 1;
    for (size_t i = 0; i < n; i++) if (w[i] < 0x20 || w[i] > 0x7E || w[i] == '"' || w[i] == '\\' || w[i] == '<' || w[i] == '>' || w[i] == '&') w[i] = '?';
    crumb.at[h] = millis() / 1000UL;
    crumb.head = (uint8_t)((h + 1) % CRUMB_REQ);
}
static String crumbRequests() {                       // the last requests, oldest first
    String reqs;
    for (uint8_t i = 0; i < CRUMB_REQ; i++) {
        uint8_t k = (uint8_t)((crumb.head + i) % CRUMB_REQ);
        if (!crumb.what[k][0]) continue;
        crumb.what[k][sizeof(crumb.what[k]) - 1] = 0;
        reqs += "+"; reqs += String((unsigned long)crumb.at[k]); reqs += "s "; reqs += crumb.what[k]; reqs += "  |  ";
    }
    return reqs;
}
static void heapWatch(const char* where) {
    if (heapDamaged) return;
    if (heap_caps_check_integrity_all(false)) { strlcpy(crumb.okAt, where, sizeof(crumb.okAt)); crumb.okUp = (uint32_t)(millis() / 1000UL); return; }
    heapDamaged = true;
    strlcpy(crumb.badAt, where, sizeof(crumb.badAt)); crumb.badUp = (uint32_t)(millis() / 1000UL);
    Serial.printf("\n[HEAP] DAMAGED. First noticed at \"%s\" (up %lu s). The check before it, at \"%s\" (up %lu s), was still clean.\n", crumb.badAt, (unsigned long)crumb.badUp, crumb.okAt, (unsigned long)crumb.okUp);
    Serial.printf("[HEAP] requests so far, oldest first: %s\n", crumbRequests().c_str());
    heap_caps_check_integrity_all(true);              // prints the damaged block itself
    // The same finding for people without a cable: a red message on the Home page right now, and kept in flash (it survives a power cut)
    String note = String("Found at \"") + crumb.badAt + "\" (up " + String((unsigned long)crumb.badUp) + " s); the check before it, at \"" + crumb.okAt + "\" (up " + String((unsigned long)crumb.okUp) + " s), was clean. Last requests: " + crumbRequests();
    strlcpy(heapLiveNote, note.c_str(), sizeof(heapLiveNote));
    prefs.putString("heapnote", heapLiveNote);
}
// Sees every request first (returns false so the real handlers still answer it)
class CrumbHandler : public AsyncWebHandler {
public:
    bool canHandle(AsyncWebServerRequest* request) const override {
        heapWatch("before a request");                // anything damaged by the request(s) before this one shows up here
        if (request) {
            crumbNote(request->methodToString(), request->url().c_str());
            request->onDisconnect([]() { heapWatch("a request was closed"); });          // damage done while the answer was being sent or the connection closed shows up here
        }
        return false;
    }
};
static CrumbHandler crumbHandler;

// The last three crash reports are kept in flash (cr0 = newest): they survive a power cut, so they can be read on the Home page (Settings > This board) days later.
static void crashStore(const String& rep) {
    String a = prefs.getString("cr1", ""), b = prefs.getString("cr0", "");
    prefs.putString("cr2", a); prefs.putString("cr1", b); prefs.putString("cr0", rep);
}

static void crumbReport(bool unclean, const char* why, uint32_t startNo) {
    if (unclean && crumb.magic == CRUMB_MAGIC) {
        String reqs = crumbRequests();
        char head[200];
        snprintf(head, sizeof(head), "restart: %s. Last check before it: up %lu s, free memory %lu KB (lowest %lu KB, largest block %lu KB), loop stage %u",
                 why, (unsigned long)crumb.upSec, (unsigned long)(crumb.freeHeap / 1024), (unsigned long)(crumb.minHeap / 1024), (unsigned long)(crumb.largest / 1024), (unsigned)crumb.stage);
        Serial.printf("[CRASH REPORT] %s\n[CRASH REPORT] last requests, oldest first: %s\n", head, reqs.length() ? reqs.c_str() : "(none)");
        crashInfo = String(head) + ". Last requests: " + (reqs.length() ? reqs : String("(none)"));
        if (crumb.badUp) {
            char hp[170];
            snprintf(hp, sizeof(hp), "HEAP DAMAGE first noticed at \"%s\" (up %lu s); the check before it, at \"%s\" (up %lu s), was clean.", crumb.badAt, (unsigned long)crumb.badUp, crumb.okAt, (unsigned long)crumb.okUp);
            Serial.printf("[CRASH REPORT] %s\n", hp);
            crashInfo = String(hp) + " " + crashInfo;
        } else Serial.println("[CRASH REPORT] the heap watch found nothing damaged before the restart (last clean check: \"" + String(crumb.okAt) + "\" at up " + String((unsigned long)crumb.okUp) + " s).");
        if (crashInfo.length() > 700) crashInfo.remove(700);
        char when[48] = "";
        if (crumb.epoch > 1700000000UL) {                                  // the clock was set before the crash: say what time it was
            int y, m, d; civilFromDays(localDayOf(crumb.epoch), &y, &m, &d);
            uint32_t s = localSecOfDay(crumb.epoch);
            snprintf(when, sizeof(when), " Clock at the last check: %04d-%02d-%02d %02u:%02u.", y, m, d, (unsigned)(s / 3600), (unsigned)((s % 3600) / 60));
        }
        crashStore(String("Start #") + String((unsigned long)startNo) + ": " + crashInfo + when);
    }
    memset(&crumb, 0, sizeof(crumb));
    crumb.magic = CRUMB_MAGIC;
}

static String portalUrl = "http://192.168.4.1/";             // where unknown addresses are sent (the captive portal): set from the real AP address at start-up

// -------------------------------------------------------------
// Pages and files from flash. The browser keeps its copy and only asks "has it changed?" (answer: 304, a few bytes); the tag changes with every
// compile, so after a new upload the browser gets the new page. That makes every page after the first one load much faster on this weak link.
// -------------------------------------------------------------
static void sendCached(AsyncWebServerRequest* r, const char* type, const char* body) {
    static const char ETAG[] = "\"" __DATE__ " " __TIME__ "\"";
    if (!r) return;
    const AsyncWebHeader* h = r->getHeader("If-None-Match");
    if (h && h->value() == ETAG) { r->send(304); return; }
    AsyncWebServerResponse* res = r->beginResponse(200, type, (const uint8_t*)body, strlen(body));      // reads straight from flash, no copy in RAM
    if (!res) { r->send(500); return; }
    res->addHeader("ETag", ETAG);
    res->addHeader("Cache-Control", "no-cache");
    r->send(res);
    heapWatch("after a page was queued");
}

// The settings pages (settings_hub.h) - included here because they use the objects and functions defined above.
#include "settings_hub.h"

// -------------------------------------------------------------
// Commands from the home page: POST /api/cmd with the form fields  c = the command  and its values.
// Until 2026-10-15 the home page used a WEBSOCKET: the board pushed the status to the page once a second from loop(), and the commands came back the same way. Every
// crash report of that time (the memory manager's bookkeeping found damaged right after / during a push, always with websocket connects and disconnects seconds earlier)
// pointed there. Now the page asks for the status itself (GET /api/status) and sends its commands as plain POSTs: all of it runs in the web server's own task.
// -------------------------------------------------------------
static String cmdArg(AsyncWebServerRequest* r, const char* name) {
    return r->hasParam(name, true) ? r->getParam(name, true)->value() : String("");
}
static void runCommand(AsyncWebServerRequest* r) {
    String cmd = cmdArg(r, "c");
    if (cmd == "reset_stats") {
        statTotalSessions = 0;
        statTotalCoins = 0;
        statTotalDurationSec = 0;
        statFinSessions = statFinCoins = statFinDurationSec = 0;
        statCanSessions = statCanCoins = statCanDurationSec = 0;
        statHistoryCount = 0;
        statHistoryHead = 0;
        prefs.putUInt("st_sess", 0);
        prefs.putUInt("st_coins", 0);
        prefs.putUInt("st_dur", 0);
        prefs.putUInt("fs_n", 0); prefs.putUInt("fs_c", 0); prefs.putUInt("fs_d", 0);
        prefs.putUInt("cs_n", 0); prefs.putUInt("cs_c", 0); prefs.putUInt("cs_d", 0);
        logReset();                                                // the session log starts empty too
        if (adj.on) { adj.on = 0; prefs.putBytes("adj_rec", &adj, sizeof(adj)); adjWithdrawLeft = 3; }      // a restart of the statistics makes the old "corrected" note and the restated block obsolete
        if (rsHave) { rsHave = false; memset(&rsPkt, 0, sizeof(rsPkt)); prefs.remove("rs_pkt"); }
        Serial.println("[MASTER CMD] Statistics counters and the session log reset.");
    } else if (cmd == "hist_clear") {                              // remove the team's history file (the machine log and the totals are not touched)
        histClear();
        Serial.println("[MASTER CMD] history.json removed.");
    } else if (cmd == "set_time") {                                // the clock: from a phone (auto = only if it is not set yet) or typed on the Home page
        uint32_t epoch = (uint32_t)strtoul(cmdArg(r, "epoch").c_str(), nullptr, 10);
        String tzs = cmdArg(r, "tz");
        int tz = tzs.length() ? (int)tzs.toInt() : (int)tzMin;
        bool autoSet = (cmdArg(r, "auto") == "1");
        if (tz < -720 || tz > 840) tz = tzMin;
        if (!(autoSet && clockIsSet())) logSetClock(epoch, (int16_t)tz, autoSet ? 1 : 2);
    } else if (cmd == "adj_restore") {                             // put the original coin total back (what was sold since stays counted)
        if (adj.on) {
            int64_t t = (int64_t)statTotalCoins + (int64_t)adj.baseCoins - (int64_t)adj.restCoins;
            statTotalCoins = (uint32_t)(t < 0 ? 0 : t);
            adjIgnoreRev = adj.rev;
            prefs.putUShort("adj_ign", adjIgnoreRev);
            prefs.putUInt("st_coins", statTotalCoins);
            adj.on = 0;
            prefs.putBytes("adj_rec", &adj, sizeof(adj));
            adjWithdrawLeft = 3;
            Serial.println("[MASTER CMD] Original coin total restored.");
        }
    } else if (cmd == "rs_clear") {                                // forget the restated data (it does not touch the statistics above)
        rsHave = false;
        memset(&rsPkt, 0, sizeof(rsPkt));
        prefs.remove("rs_pkt");
        Serial.println("[MASTER CMD] Restated data removed.");
    } else if (cmd == "crash_clear") {                             // Settings > This board > Delete: forget the stored crash reports
        prefs.remove("cr0"); prefs.remove("cr1"); prefs.remove("cr2"); prefs.remove("heapnote");
        crashInfo = ""; heapLiveNote[0] = 0;
        Serial.println("[MASTER CMD] Stored crash reports deleted.");
    } else if (cmd == "save_coin_value") {
        String vs = cmdArg(r, "val");
        float v = vs.length() ? vs.toFloat() : coinValuePeso;
        if (v >= 0.1f && v <= 1000.0f) {
            coinValuePeso = v;
            prefs.putFloat("coin_val", coinValuePeso);
            Serial.printf("[MASTER CMD] Coin value set to PHP %.2f per pulse.\n", coinValuePeso);
        }
    }
}

// -------------------------------------------------------------
// Core 3.3.7 ESP-NOW Receive Callback
//
// The callback runs inside the Wi-Fi task (a small stack, and everything the radio does waits for it), and with all three boxes and both
// ACS boards online this node hears dozens of packets a second. So the callback does NOTHING but copy the packet into a queue; loop()
// does all the work (remembering it for the pages, the revenue tally with its flash writes, the log lines). Same pattern as the S3 nodes.
// -------------------------------------------------------------
struct RxItem {
    uint8_t len;
    uint8_t mac[6];
    uint8_t data[sizeof(CommandPacket)];
};
#define RXQ_SIZE 32
static portMUX_TYPE rxMux = portMUX_INITIALIZER_UNLOCKED;
RxItem rxQueue[RXQ_SIZE];
volatile uint8_t  rxHead = 0;
volatile uint8_t  rxTail = 0;
volatile uint32_t rxDropped = 0;    // packets lost because loop() was busy for too long (shown on the home page)

void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
    if (!info || !info->src_addr || !incomingData || len <= 0 || len > (int)sizeof(CommandPacket)) return;
    portENTER_CRITICAL(&rxMux);
    uint8_t next = (uint8_t)((rxHead + 1) % RXQ_SIZE);
    if (next != rxTail) {
        RxItem& it = rxQueue[rxHead];
        it.len = (uint8_t)len;
        memcpy(it.mac, info->src_addr, 6);
        memcpy(it.data, incomingData, (size_t)len);
        rxHead = next;
    } else {
        rxDropped = rxDropped + 1;
    }
    portEXIT_CRITICAL(&rxMux);
}

// Everything that used to run inside the callback; now runs from loop().
void processPacket(const uint8_t* mac, const uint8_t* incomingData, int len) {
    hubOnPacket(incomingData, len);                     // the settings pages remember everything they display

    if (len == (int)sizeof(CommandPacket)) return;      // commands / the nodes' settings broadcasts: handled by the hub, nothing else to do

    if (len == (int)sizeof(RestatePacket)) { restateOnPacket(incomingData); return; }   // the Update button of the spare ESP32_RESTATE board

    if (len == (int)sizeof(CoinBookPacket)) {                                           // Box 1's / Box 3's running total of booked coins
        CoinBookPacket cb;
        memcpy(&cb, incomingData, sizeof(cb));
        if (cb.magic == COINBOOK_MAGIC) {
            if (cb.deviceID == DEVICE_S3A)      { learnMac(DEVICE_S3A, mac); trackBooked(0, cb.bookedCoins); }
            else if (cb.deviceID == DEVICE_S3B) { trackBooked(2, cb.bookedCoins); }
        }
        return;
    }

    if (len == (int)sizeof(Box1ConfigPacket)) { learnMac(DEVICE_S3A, mac); return; }   // legacy config broadcasts: only useful to learn the MAC
    if (len == (int)sizeof(Box2ConfigPacket)) { learnMac(DEVICE_S3C, mac); return; }

    // Live status of each box's S3 node (same packet shape, the deviceID says which one)
    if (len == (int)sizeof(BoxStatusPacket)) {
        BoxStatusPacket status;
        memcpy(&status, incomingData, sizeof(BoxStatusPacket));
        if (status.deviceID == DEVICE_S3A)      { learnMac(DEVICE_S3A, mac); trackSession(0, status); }
        else if (status.deviceID == DEVICE_S3C) { learnMac(DEVICE_S3C, mac); trackSession(1, status); }
        else if (status.deviceID == DEVICE_S3B) { trackSession(2, status); }
        return;
    }

    if (len != (int)sizeof(TelemetryPacket)) return;

    TelemetryPacket packet;
    memcpy(&packet, incomingData, sizeof(TelemetryPacket));

    if (packet.deviceID == DEVICE_A1)      { learnMac(DEVICE_A1, mac); trackCoins(0, packet.pulseCount); }
    else if (packet.deviceID == DEVICE_C1) { learnMac(DEVICE_C1, mac); trackCoins(1, packet.pulseCount); }
    else if (packet.deviceID == DEVICE_B1) { trackCoins(2, packet.pulseCount); }
}

// Called from loop(): handles up to 16 queued packets per pass.
void drainRxQueue() {
    for (uint8_t n = 0; n < 16; n++) {
        RxItem it;
        bool got = false;
        portENTER_CRITICAL(&rxMux);
        if (rxTail != rxHead) {
            memcpy(&it, &rxQueue[rxTail], sizeof(RxItem));
            rxTail = (uint8_t)((rxTail + 1) % RXQ_SIZE);
            got = true;
        }
        portEXIT_CRITICAL(&rxMux);
        if (!got) return;
        processPacket(it.mac, it.data, it.len);
    }
}

uint32_t bootCount = 0;      // every start since the sketch was first flashed
uint32_t crashCount = 0;     // starts that were NOT a normal power-on / reset button / software restart

// The home page's status (GET /api/status, asked for once a second by every open Dashboard)
static void buildStatusJson(String& output) {
            JsonDocument doc;
            doc["heap"]       = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
            doc["free_psram"] = heap_caps_get_free_size(MALLOC_CAP_SPIRAM); // 0 if PSRAM isn't actually initialized
            hubFillWs(doc);

            // board health for the home page
            doc["up"]       = (uint32_t)(millis() / 1000UL);
            doc["rst"]      = hubResetReason();
            doc["boots"]    = bootCount;
            doc["crashes"]  = crashCount;
            doc["minheap"]  = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
            doc["rxdrop"]   = (uint32_t)rxDropped;
            doc["clients"]  = (int)WiFi.softAPgetStationNum();
            if (crashInfo.length()) doc["crash"] = crashInfo;             // what the board was doing before its last unexpected restart
            if (heapLiveNote[0]) doc["heap_bad"] = String(heapLiveNote);  // the heap watch found damage and the board is still running

            doc["coin_value"]    = coinValuePeso;
            doc["stat_sessions"] = statTotalSessions;
            doc["stat_revenue"]  = statTotalCoins * coinValuePeso;
            doc["stat_coins"]    = statTotalCoins;
            doc["stat_avg_dur"]  = (statTotalSessions > 0) ? (statTotalDurationSec / statTotalSessions) : 0;
            logFillWs(doc);                                               // the clock, the log storage and the daily / weekly usage

            // finished vs cancelled sessions (counted since the firmware that added them)
            doc["fin_n"]     = statFinSessions;
            doc["fin_coins"] = statFinCoins;
            doc["fin_rev"]   = statFinCoins * coinValuePeso;
            doc["fin_avg"]   = (statFinSessions > 0) ? (statFinDurationSec / statFinSessions) : 0;
            doc["fin_dur"]   = statFinDurationSec;                          // the sums too, so the page can add the JSON file's numbers to them
            doc["can_dur"]   = statCanDurationSec;
            doc["can_n"]     = statCanSessions;
            doc["can_coins"] = statCanCoins;
            doc["can_rev"]   = statCanCoins * coinValuePeso;
            doc["can_avg"]   = (statCanSessions > 0) ? (statCanDurationSec / statCanSessions) : 0;

            // corrected statistics (a mode-1 update was applied): what the page needs for its one-line note
            doc["adj_on"] = (bool)adj.on;
            if (adj.on) {
                doc["adj_rev"]        = (uint32_t)adj.rev;
                doc["adj_at"]         = (uint32_t)adj.capturedAt;
                doc["adj_tz"]         = (int)adj.tzMin;
                doc["adj_base_coins"] = (uint32_t)adj.baseCoins;
                doc["adj_base_rev"]   = adj.baseCoins * (adj.cvX100 / 100.0);
                doc["adj_rest_coins"] = (uint32_t)adj.restCoins;
            }
            // restated old data (from the spare ESP32_RESTATE board), computed here so the page only shows it
            doc["rs_on"] = rsHave;
            if (rsHave) {
                double cvOld = rsPkt.cvX100 / 100.0;
                bool down = (statTotalCoins < rsPkt.baseCoins) || (statTotalSessions < rsPkt.baseSessions);
                uint32_t newCn = down ? 0 : statTotalCoins - rsPkt.baseCoins;
                uint32_t newSs = down ? 0 : statTotalSessions - rsPkt.baseSessions;
                double newRev  = newCn * coinValuePeso;
                doc["rs_rev"]         = (uint32_t)rsPkt.rev;
                doc["rs_at"]          = (uint32_t)rsPkt.capturedAt;
                doc["rs_tz"]          = (int)rsPkt.tzMin;
                doc["rs_cpm"]         = rsPkt.cpmNewX100 / 100.0;
                doc["rs_down"]        = down;
                doc["rs_old_ss"]      = (uint32_t)rsPkt.baseSessions;
                doc["rs_old_coins"]   = (uint32_t)rsPkt.baseCoins;
                doc["rs_old_rev"]     = rsPkt.baseCoins * cvOld;
                doc["rs_old_rcoins"]  = (uint32_t)rsPkt.oldRestCoins;
                doc["rs_old_rrev"]    = rsPkt.oldRestCoins * cvOld;
                doc["rs_new_ss"]      = newSs;
                doc["rs_new_coins"]   = newCn;
                doc["rs_new_rev"]     = newRev;
                doc["rs_tot_coins"]   = rsPkt.baseCoins + newCn;
                doc["rs_tot_rcoins"]  = rsPkt.oldRestCoins + newCn;
                doc["rs_tot_rev"]     = rsPkt.baseCoins * cvOld + newRev;
                doc["rs_tot_rrev"]    = rsPkt.oldRestCoins * cvOld + newRev;
            }

            JsonArray hist = doc.createNestedArray("stat_history");
            for (uint8_t i = 0; i < statHistoryCount; i++) {
                uint8_t idx = (statHistoryHead + STAT_HISTORY_SIZE - statHistoryCount + i) % STAT_HISTORY_SIZE;
                JsonObject rec = hist.createNestedObject();
                rec["d"] = statHistory[idx].durationSec;
                rec["r"] = statHistory[idx].coinsUsed * coinValuePeso;
                rec["x"] = statHistory[idx].cancelled;          // 1 = cancelled (aborted mid-cycle)
            }

            output.reserve(measureJson(doc) + 8);                        // one allocation instead of a chain of growing ones every second
            serializeJson(doc, output);
}

// Why did this board last restart? Shown on the home page so a power dip (BROWNOUT) can be told apart from a crash or a watchdog.

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("\n=======================================================");
    Serial.println("   LOCAL SERVER: CONTROL CENTER (CORE 3.3.7)");
    Serial.println("=======================================================");

    loadSettingsFromNVS();
    logBegin();                           // the session log (flash file system) and the clock's time zone
    {
        esp_reset_reason_t why = esp_reset_reason();
        bool unclean = !(why == ESP_RST_POWERON || why == ESP_RST_SW || why == ESP_RST_EXT);
        bootCount = prefs.getUInt("boot_cnt", 0) + 1;
        prefs.putUInt("boot_cnt", bootCount);
        crashCount = prefs.getUInt("crash_cnt", 0);
        if (unclean) { crashCount++; prefs.putUInt("crash_cnt", crashCount); }
        Serial.printf("[BOOT] last restart: %s | starts %u | unexpected restarts %u%s\n", hubResetReason(), (unsigned)bootCount, (unsigned)crashCount,
                      why == ESP_RST_BROWNOUT ? "  <-- BROWNOUT: the power supply dipped, check the supply / cable" : "");
        crumbReport(unclean, hubResetReason(), bootCount);
    }
    hubBegin();

    // 1. SoftAP Setup
    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false);
    WiFi.setTxPower(WIFI_POWER_5dBm);
    WiFi.softAP(AP_SSID, AP_PASSWORD, ESPNOW_WIFI_CHANNEL, 0, 4);

    IPAddress apIP = WiFi.softAPIP();
    portalUrl = "http://" + apIP.toString() + "/";
    Serial.printf("[MASTER WIFI] SoftAP Online -> SSID: '%s' | IP: %s\n", AP_SSID, apIP.toString().c_str());

    // 2. Captive Portal DNS
    dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer.start(DNS_PORT, "*", apIP);
    Serial.println("[MASTER DNS] Captive Portal active on Port 53.");

    // 3. ESP-NOW Initialization & Broadcast Peer Registration
    if (esp_now_init() != ESP_OK) {
        Serial.println("[MASTER ESP-NOW] Initialization Failed!");
        return;
    }
    esp_now_register_send_cb(onDataSent);
    esp_now_register_recv_cb(onDataRecv);

    esp_now_peer_info_t peerInfo;
    memset(&peerInfo, 0, sizeof(esp_now_peer_info_t));
    memcpy(peerInfo.peer_addr, BROADCAST_MAC, 6);
    peerInfo.channel = ESPNOW_WIFI_CHANNEL;
    peerInfo.encrypt = false;

    if (esp_now_add_peer(&peerInfo) == ESP_OK) {
        Serial.println("[MASTER ESP-NOW] Broadcast Peer {FF:FF:FF:FF:FF:FF} Registered on Channel 1.");
    }

    // 4. Web server: home page, the four settings pages and their API, then everything else falls back to the home page (captive portal)
    server.addHandler(&crumbHandler);     // first in line: notes every request for the crash report, answers none

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) { sendCached(request, "text/html", INDEX_HTML); });
    server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request) { sendCached(request, "text/css", STYLE_CSS); });
    server.on("/script.js", HTTP_GET, [](AsyncWebServerRequest *request) { sendCached(request, "application/javascript", SCRIPT_JS); });
    server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request) { if (request) request->send(204); });      // browsers ask for it on every page: not the whole home page again

    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {                       // the home page asks for this once a second
        if (!request) return;
        String out;
        buildStatusJson(out);
        AsyncWebServerResponse* res = request->beginResponse(200, "application/json", out);
        res->addHeader("Cache-Control", "no-store");
        request->send(res);
    });
    server.on("/api/crashes", HTTP_GET, [](AsyncWebServerRequest *request) {                      // the stored crash reports (newest first) for Settings > This board
        if (!request) return;
        JsonDocument doc;
        JsonArray a = doc["reports"].to<JsonArray>();
        for (int i = 0; i < 3; i++) { String s = prefs.getString(i == 0 ? "cr0" : (i == 1 ? "cr1" : "cr2"), ""); if (s.length()) a.add(s); }
        String hn = prefs.getString("heapnote", "");
        if (hn.length()) doc["heapnote"] = hn;
        String out;
        serializeJson(doc, out);
        AsyncWebServerResponse* res = request->beginResponse(200, "application/json", out);
        res->addHeader("Cache-Control", "no-store");
        request->send(res);
    });
    server.on("/api/cmd", HTTP_POST, [](AsyncWebServerRequest *request) {                          // the home page's commands (clock, Reset Stats, coin value, ...)
        if (!request) return;
        runCommand(request);
        request->send(200, "application/json", "{\"ok\":1}");
    });

    hubRegisterRoutes();
    logRegisterRoutes();                  // /log.csv

    // Phones and PCs ask well-known addresses (/generate_204, /hotspot-detect.html, ...) to find out whether this Wi-Fi has internet; the answer that makes them show the
    // "sign in" screen is anything but their expected one. They give up on the connection as soon as they see it is not the expected one, so a 17 KB answer was abandoned
    // half way again and again (bursts of several probes within a second) - the crashes that followed were in the memory of those connections. A short redirect to the home page
    // does the same job (the sign-in screen opens the home page) and is over in one packet.
    server.onNotFound([](AsyncWebServerRequest *request) {
        if (request) request->redirect(portalUrl);
    });

    server.begin();
    Serial.println("[MASTER HTTP] Web Server Online.");
}

void loop() {
    crumb.stage = 1; drainRxQueue();      // everything the radio heard since the last pass
    { static uint32_t lastRadioChk = 0; if (millis() - lastRadioChk > 250) { lastRadioChk = millis(); heapWatch("after radio packets"); } }
    crumb.stage = 2; dnsServer.processNextRequest();
    crumb.stage = 3;

    unsigned long currentMillis = millis();

    // 1-Second Master Execution Tick
    if (currentMillis - lastOneSecTick >= 1000) {
        lastOneSecTick = currentMillis;
        crumb.stage = 4;                  // for the crash report: where we were, and the memory right now
        heapWatch("every second: start");
        crumb.upSec = (uint32_t)(currentMillis / 1000UL);
        crumb.freeHeap = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        crumb.minHeap = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
        crumb.largest = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
        crumb.epoch = nowEpoch();

        // Heartbeat: every node can tell whether anybody is coordinating it (the S3 nodes broadcast their own equivalent).
        // subIndex identifies the sender since CommandPacket has no sender field.
        {
            CommandPacket hb;
            memset(&hb, 0, sizeof(CommandPacket));
            hb.targetDeviceID = 0; // broadcast - every node listens for this opcode regardless
            hb.commandID      = CMD_HEARTBEAT;
            hb.subIndex       = DEVICE_SERVER;
            esp_now_send(BROADCAST_MAC, (uint8_t*)&hb, sizeof(CommandPacket));
        }

        // The restore of a correction is announced to the Online Server (a "withdrawn" RestatePacket), three seconds in a row
        if (adjWithdrawLeft) {
            RestatePacket k;
            memset(&k, 0, sizeof(k));
            k.deviceID = DEVICE_SERVER; k.magic = RESTATE_MAGIC; k.active = 0; k.mode = 1; k.rev = adj.rev;
            esp_now_send(BROADCAST_MAC, (uint8_t*)&k, sizeof(k));
            adjWithdrawLeft--;
        }

        crumb.stage = 5;
        logTick();                                                        // session log housekeeping (daily chart numbers, storage numbers, a stalled push)
        heapWatch("after log housekeeping");

        // Revenue totals for the Online Server, every 10 seconds
        crumb.stage = 6;
        heapWatch("before hub stats");
        static uint8_t statsTick = 0;
        if (++statsTick >= 10) { statsTick = 0; broadcastHubStats(); }

        heapWatch("end of the second");
        crumb.stage = 0;
    }
}
