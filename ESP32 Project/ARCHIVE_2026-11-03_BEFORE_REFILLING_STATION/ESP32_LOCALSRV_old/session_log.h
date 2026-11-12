// =====================================================================
//  SESSION LOG + CLOCK (2026-10-14)  - included by ESP32_LOCALSRV.ino (after the statistics, prefs and ESP-NOW helpers it uses exist).
//
//  What it is: a permanent, timestamped list of every customer session the MACHINES report, kept in flash (a file, so it survives restarts and normal
//  reflashes). It never stores a name. Each entry is 16 bytes, so 20,000 entries (about 320 KB) fit easily in the 9.9 MB file partition.
//  The only way an entry gets into the log is recordCompletedSession() in ESP32_LOCALSRV.ino, i.e. a box's S3 said a session ended.
//  There is deliberately no way to add, change or remove an entry: "Reset Stats" empties the whole log, nothing else changes it.
//  (Two exceptions, neither is an edit: a session saved while the clock was not set gets its real time once the clock is set; and BACKUP RESTORE below.)
//
//  BACKUP / RESTORE: /log.csv is the backup. Its last line is "#integrity,<sha256>" over every line above it (with a fixed salt). Settings > "Restore from backup CSV"
//  accepts ONLY a file whose integrity line matches, i.e. an unchanged download of this log (a file edited by hand or saved again in a spreadsheet is refused).
//  It merges: sessions already in the log stay, rows that are already there are not added twice, so it can be used after the log was lost (a damaged flash file system
//  is re-created at start-up) without losing the sessions recorded since. Corrections never go through this door: they belong in the JSON history file, which is labelled.
//
//  CLOCK: the LocalServer has no real-time clock and is not on 24/7. The time comes from a phone that opens the Control Center (automatic, once per
//  power-on) or from the date and time typed on the Home page. Until it is set, sessions are saved as "no date yet" together with the seconds since
//  power-on; the moment the clock is set, every such session of this power-on gets its real date and time. After a power cut the clock is gone until
//  a phone sets it again. Local midnight (for the daily chart) uses the time zone the phone reported (default UTC+8).
//
//  The Home page shows the usage per day (last 7 days) or per week (last 12 weeks) from this log, the storage used / free, and /log.csv downloads it.
//  Entries older than a year are dropped automatically (once the clock is set); the totals are cumulative counters and are not affected.
//
//  HISTORY FILE (history.json): the days before the log existed (the LocalServer only counted revenue up until the log was added) can be shown from a JSON file
//  the team edits and uploads on the Home page: previous customers collected by the team from its own evidence (survey forms, photos, videos ...), one row per customer
//    {"source":"previous data collected after the upgrade","rows":[["2026-09-17","14:35",1,15,"F",5], ...]}      date, time, box 1-3, coins, F finished / C cancelled, minutes (optional)
//  The daily / weekly chart reads BOTH files and draws them in the same card; the part that comes from the history file is drawn in a lighter shade and the
//  chart carries the legend "lighter = previous data collected by the team after the upgrade". That marking is part of the chart and has no switch. The history file never changes the machine
//  log, the totals at the top of the Home page, the CSV or what the Online Server gets: those stay what the machines recorded. Only the Finished / Cancelled card adds it
//  (and says how much came from it).
// =====================================================================
#pragma once
#include <FFat.h>
#include <LittleFS.h>
#include <esp_timer.h>
#include <algorithm>
#include "mbedtls/sha256.h"

// ---------------------------------------------------------------------
// Clock
// ---------------------------------------------------------------------
static uint32_t clkEpoch = 0;                  // unix time at the moment clkUp
static uint32_t clkUp = 0;                     // seconds since power-on at that moment
static int16_t  tzMin = 480;                   // minutes east of UTC (the Philippines: +480)
static uint8_t  clkSrc = 0;                    // 0 not set, 1 set from a phone, 2 typed in by hand
static uint16_t logBootId = 1;                 // changes at every power-on (links "no date yet" sessions to this power-on)

static inline uint32_t upSec() { return (uint32_t)(esp_timer_get_time() / 1000000LL); }
static inline bool     clockIsSet() { return clkEpoch > 1700000000UL; }
static inline uint32_t nowEpoch() { return clockIsSet() ? clkEpoch + (upSec() - clkUp) : 0; }
static inline int32_t  localDayOf(uint32_t t) { return (int32_t)(((int64_t)t + (int64_t)tzMin * 60) / 86400); }          // days since 1970-01-01, local
static inline uint32_t localSecOfDay(uint32_t t) { int64_t v = ((int64_t)t + (int64_t)tzMin * 60) % 86400; return (uint32_t)(v < 0 ? v + 86400 : v); }

static void civilFromDays(int32_t z, int* y, int* m, int* d) {      // days since 1970-01-01 -> y, m, d
    z += 719468;
    int32_t era = (z >= 0 ? z : z - 146096) / 146097;
    uint32_t doe = (uint32_t)(z - era * 146097);
    uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int32_t yy = (int32_t)yoe + era * 400;
    uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    uint32_t mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = (int)(yy + (*m <= 2 ? 1 : 0));
}
static uint32_t ymdOfDays(int32_t days) { int y, m, d; civilFromDays(days, &y, &m, &d); return (uint32_t)(y * 10000 + m * 100 + d); }
static int32_t daysFromCivil(int y, int m, int d) {
    y -= (m <= 2) ? 1 : 0;
    int32_t era = (y >= 0 ? y : y - 399) / 400;
    uint32_t yoe = (uint32_t)(y - era * 400);
    uint32_t doy = (uint32_t)((153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1);
    uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int32_t)doe - 719468;
}
static bool daysOfYmdParts(int y, int m, int d, int32_t* days) {      // false when it is not a real date (e.g. 31 February)
    if (y < 2020 || y > 2099 || m < 1 || m > 12 || d < 1 || d > 31) return false;
    int32_t z = daysFromCivil(y, m, d);
    if (ymdOfDays(z) != (uint32_t)(y * 10000 + m * 100 + d)) return false;
    *days = z; return true;
}

// ---------------------------------------------------------------------
// The journal file: one 16-byte entry per session the machines reported
// ---------------------------------------------------------------------
#define JF_CANCEL 1                            // the session was aborted by the customer (shown as Cancelled)
#define JF_NOTIME 2                            // t = seconds since power-on of bootId (the clock was not set yet)
struct JEnt {                                  // 16 bytes
    uint8_t  kind, box, flags, pad;            // kind is always 1 (a session reported by a box)
    uint32_t t;                                // unix time the session ended (or seconds since power-on, see JF_NOTIME)
    uint16_t coins, dur;                       // coins paid, session length in seconds
    uint16_t ref, bootId;                      // ref unused; bootId = the power-on the session belongs to
} __attribute__((packed));
static_assert(sizeof(JEnt) == 16, "a journal entry must be 16 bytes");

static fs::FS* logFs = nullptr;
static bool     logOk = false, logUsesFat = false, logFull = false, logDirty = true;
static bool     logReformatted = false;        // the log storage could not be mounted at THIS start and was re-created empty
static uint32_t logFmtCount = 0;               // how often that happened since the board was first flashed
static const char* const LOG_PATH = "/sessions.bin";
static const uint32_t LOG_CAP = 20000;         // entries (about 320 KB)
static uint32_t logCount = 0;                  // entries in the file
static uint32_t fsTotalCached = 0, fsUsedCached = 0;
static SemaphoreHandle_t logLock = nullptr;
struct LogGuard { LogGuard() { if (logLock) xSemaphoreTakeRecursive(logLock, portMAX_DELAY); } ~LogGuard() { if (logLock) xSemaphoreGiveRecursive(logLock); } };

template <typename F> static void jScan(F fn) {              // fn(index, entry) for every entry, in order
    if (!logOk || logCount == 0) return;
    File f = logFs->open(LOG_PATH, "r");
    if (!f) return;
    JEnt buf[32];
    uint32_t idx = 0;
    while (idx < logCount) {
        uint32_t want = logCount - idx; if (want > 32) want = 32;
        size_t n = f.read((uint8_t*)buf, want * sizeof(JEnt)) / sizeof(JEnt);
        if (n == 0) break;
        for (size_t i = 0; i < n; i++) fn(idx + (uint32_t)i, buf[i]);
        idx += (uint32_t)n;
        if (n < want) break;
    }
    f.close();
}
static size_t jRead(uint32_t idx, JEnt* out, uint32_t n) {
    File f = logFs->open(LOG_PATH, "r");
    if (!f) return 0;
    f.seek((size_t)idx * sizeof(JEnt));
    size_t got = f.read((uint8_t*)out, n * sizeof(JEnt)) / sizeof(JEnt);
    f.close();
    return got;
}
static bool jAppend(const JEnt& e) {
    LogGuard g;
    if (!logOk) return false;
    if (logCount >= LOG_CAP) { logFull = true; return false; }
    File f = logFs->open(LOG_PATH, "r+");
    if (!f) { File c = logFs->open(LOG_PATH, "w"); if (!c) return false; c.close(); f = logFs->open(LOG_PATH, "r+"); if (!f) return false; }
    f.seek((size_t)logCount * sizeof(JEnt));
    size_t w = f.write((const uint8_t*)&e, sizeof(e));
    f.close();
    if (w != sizeof(e)) return false;
    logCount++;
    logDirty = true;
    return true;
}

// ---------------------------------------------------------------------
// Setup, mount, clock
// ---------------------------------------------------------------------
static void histLoad();                        // (defined with the history file below)
static void logRefreshFsNumbers() {
    if (!logOk) { fsTotalCached = fsUsedCached = 0; return; }
    fsTotalCached = logUsesFat ? (uint32_t)FFat.totalBytes() : (uint32_t)LittleFS.totalBytes();
    fsUsedCached  = logUsesFat ? (uint32_t)FFat.usedBytes()  : (uint32_t)LittleFS.usedBytes();
}
static void logBegin() {
    logLock = xSemaphoreCreateRecursiveMutex();
    logBootId = (uint16_t)(esp_random() & 0xFFFF); if (logBootId == 0) logBootId = 1;
    tzMin = prefs.getShort("tz_min", 480);
    // A file system that mounted before and now does not (a power cut in the middle of a write can damage a FAT volume) is re-created EMPTY by begin(true).
    // That must not happen silently: it is remembered and shown on the Home page, so somebody restores the last backup CSV.
    bool seen = prefs.getBool("log_seen", false);
    logFmtCount = prefs.getUInt("fmt_cnt", 0);
    if (FFat.begin(false)) { logFs = &FFat; logUsesFat = true; logOk = true; }
    else if (FFat.begin(true)) {
        logFs = &FFat; logUsesFat = true; logOk = true;
        if (seen) { logReformatted = true; logFmtCount++; prefs.putUInt("fmt_cnt", logFmtCount); Serial.println("[LOG] The log storage was damaged and has been re-created EMPTY. Restore the last backup CSV (Settings)."); }
    }
    else if (LittleFS.begin(true)) { logFs = &LittleFS; logUsesFat = false; logOk = true; }
    if (logOk && !seen) prefs.putBool("log_seen", true);
    if (!logOk) {
        Serial.println("[LOG] NO STORAGE for the session log. Arduino IDE: Tools > Partition Scheme > '16M Flash (3MB APP/9.9MB FATFS)', then upload again. The statistics still work, the log and its chart are off.");
        return;
    }
    if (!logFs->exists(LOG_PATH) && logFs->exists("/sessions.new")) logFs->rename("/sessions.new", LOG_PATH);      // a clean-up that was interrupted by a power cut
    File f = logFs->open(LOG_PATH, "r");
    logCount = f ? (uint32_t)(f.size() / sizeof(JEnt)) : 0;
    if (f) f.close();
    if (logCount > LOG_CAP) logCount = LOG_CAP;
    logRefreshFsNumbers();
    Serial.printf("[LOG] storage %s: %lu of %lu entries used, %lu KB free\n", logUsesFat ? "FFat" : "LittleFS", (unsigned long)logCount, (unsigned long)LOG_CAP,
                  (unsigned long)((fsTotalCached - fsUsedCached) / 1024));
    histLoad();                                // the team's history file, if there is one
}

static void logFixTimes() {                    // the clock was just set: give every "no date yet" session of THIS power-on its real time
    LogGuard g;
    if (!logOk || !clockIsSet() || logCount == 0) return;
    File f = logFs->open(LOG_PATH, "r+");
    if (!f) return;
    uint32_t nowE = nowEpoch(), nowU = upSec(), idx = 0, fixed = 0;
    JEnt buf[32];
    while (idx < logCount) {
        uint32_t want = logCount - idx; if (want > 32) want = 32;
        f.seek((size_t)idx * sizeof(JEnt));
        size_t n = f.read((uint8_t*)buf, want * sizeof(JEnt)) / sizeof(JEnt);
        if (n == 0) break;
        bool mod = false;
        for (size_t i = 0; i < n; i++) {
            JEnt& e = buf[i];
            if ((e.flags & JF_NOTIME) && e.bootId == logBootId) {
                uint32_t ago = (nowU >= e.t) ? nowU - e.t : 0;
                e.t = nowE - ago; e.flags &= (uint8_t)~JF_NOTIME; mod = true; fixed++;
            }
        }
        if (mod) { f.seek((size_t)idx * sizeof(JEnt)); f.write((const uint8_t*)buf, n * sizeof(JEnt)); }
        idx += (uint32_t)n;
    }
    f.close();
    if (fixed) { logDirty = true; Serial.printf("[LOG] clock set: %lu session(s) of this power-on got their date and time\n", (unsigned long)fixed); }
}
static void logSetClock(uint32_t epoch, int16_t tz, uint8_t src) {
    if (epoch < 1700000000UL) return;
    clkEpoch = epoch; clkUp = upSec(); clkSrc = src;
    if (tz != tzMin) { tzMin = tz; prefs.putShort("tz_min", tzMin); }
    Serial.printf("[LOG] clock set from %s: unix %lu, time zone %+d min\n", src == 1 ? "a phone" : "the Home page", (unsigned long)epoch, (int)tz);
    logFixTimes();
    logDirty = true;
}

// A session a box just reported (called from recordCompletedSession, which also updates the counters)
void logAppendMachine(uint8_t boxNo, bool cancelled, uint16_t coins, uint16_t durSec) {
    JEnt e; memset(&e, 0, sizeof(e));
    e.kind = 1; e.box = boxNo; e.flags = cancelled ? JF_CANCEL : 0; e.coins = coins; e.dur = durSec; e.bootId = logBootId;
    if (clockIsSet()) e.t = nowEpoch(); else { e.flags |= JF_NOTIME; e.t = upSec(); }
    if (!jAppend(e)) Serial.println("[LOG] could not save a session (storage missing or full)");
}

// "Reset Stats" also empties the log (the caller zeroes the counters)
void logReset() {
    LogGuard g;
    if (logOk && logFs) logFs->remove(LOG_PATH);
    logCount = 0; logFull = false; logDirty = true;
    logRefreshFsNumbers();
}

// ---------------------------------------------------------------------
// The team's history file (history.json) - see the header. Rows are read into RAM; the file is never rewritten by the LocalServer.
// ---------------------------------------------------------------------
#define HIST_PATH "/history.json"
#define HIST_TMP  "/history.tmp"
#define HIST_MAX_ROWS 1000
#define HIST_MAX_BYTES 65536
struct HRow { int32_t day; uint16_t coins; uint8_t cancel, box; uint16_t tod; uint16_t durSec; };      // tod = minutes since midnight, durSec 0xFFFF = not given
// The rows live in PSRAM when the board has it (16 KB), so they do not eat the internal RAM the web server and the radio need; internal RAM is the fallback.
static HRow*    hist = nullptr;
static bool histAlloc() {
    if (hist) return true;
    hist = (HRow*)heap_caps_malloc(sizeof(HRow) * HIST_MAX_ROWS, MALLOC_CAP_SPIRAM);
    if (!hist) hist = (HRow*)malloc(sizeof(HRow) * HIST_MAX_ROWS);
    return hist != nullptr;
}
static uint16_t histN = 0;
static uint32_t histFromYmd = 0, histToYmd = 0;
struct HistTot { uint32_t n, coins, durSec, durN; };      // durN = the rows that give a length (the minutes are optional)
static HistTot histFin = {0, 0, 0, 0}, histCan = {0, 0, 0, 0};      // totals of the WHOLE file, finished and cancelled (the chart only looks at the last 100 days)
static char     histMsg[120] = "";             // why the file was not accepted / read (shown on the Home page)

static void histSplit(const String& row, String* tok, int* nt) {      // the values of one [ ... ] row: split at commas that are not inside quotes
    *nt = 0;
    bool inq = false; String cur;
    for (unsigned i = 0; i <= row.length(); i++) {
        char c = (i < row.length()) ? row[i] : ',';
        if (c == '"') { inq = !inq; continue; }
        if (c == ',' && !inq) { cur.trim(); if (*nt < 8) tok[(*nt)++] = cur; cur = ""; continue; }
        cur += c;
    }
}
static bool histParseText(const String& txt, HRow* out, uint16_t* nOut, char* err, size_t errCap) {
    int p = txt.indexOf("\"rows\"");
    if (p < 0) { snprintf(err, errCap, "No \"rows\" list found in the file."); return false; }
    p = txt.indexOf('[', p);
    if (p < 0) { snprintf(err, errCap, "The \"rows\" list has no [ ]."); return false; }
    p++;
    uint16_t n = 0; int rowNo = 0;
    while (true) {
        while (p < (int)txt.length() && txt[p] != '[' && txt[p] != ']') p++;
        if (p >= (int)txt.length()) { snprintf(err, errCap, "The file ends inside the rows list."); return false; }
        if (txt[p] == ']') break;                                     // the end of the rows list
        int e = txt.indexOf(']', p);
        if (e < 0) { snprintf(err, errCap, "Row %d is not closed with ].", rowNo + 1); return false; }
        String row = txt.substring(p + 1, e);
        p = e + 1; rowNo++;
        String tok[8]; int nt = 0;
        histSplit(row, tok, &nt);
        if (nt < 5) { snprintf(err, errCap, "Row %d: needs date, time, box, coins, F/C (and optional minutes).", rowNo); return false; }
        int y = 0, m = 0, d = 0, hh = 0, mi = 0; int32_t days;
        if (sscanf(tok[0].c_str(), "%d-%d-%d", &y, &m, &d) != 3 || !daysOfYmdParts(y, m, d, &days)) { snprintf(err, errCap, "Row %d: the date '%s' is not a real date (use YYYY-MM-DD).", rowNo, tok[0].c_str()); return false; }
        if (sscanf(tok[1].c_str(), "%d:%d", &hh, &mi) != 2 || hh < 0 || hh > 23 || mi < 0 || mi > 59) { snprintf(err, errCap, "Row %d: the time '%s' is not HH:MM.", rowNo, tok[1].c_str()); return false; }
        long box = tok[2].toInt(), coins = tok[3].toInt();
        if (box < 1 || box > 3) { snprintf(err, errCap, "Row %d: the box must be 1, 2 or 3.", rowNo); return false; }
        if (coins < 1 || coins > 999) { snprintf(err, errCap, "Row %d: the coins must be 1 to 999.", rowNo); return false; }
        char sc = tok[4].length() ? tok[4][0] : 0;
        if (sc != 'F' && sc != 'f' && sc != 'C' && sc != 'c') { snprintf(err, errCap, "Row %d: the status must be F (finished) or C (cancelled).", rowNo); return false; }
        float mins = -1.0f;                                          // minutes may have decimals (4.5 = 4 min 30 s); empty = not given
        if (nt >= 6 && tok[5].length()) { mins = tok[5].toFloat(); if (!(mins >= 0.0f && mins <= 720.0f)) { snprintf(err, errCap, "Row %d: the minutes must be 0 to 720.", rowNo); return false; } }
        if (n >= HIST_MAX_ROWS) { snprintf(err, errCap, "Too many rows (%d at most).", HIST_MAX_ROWS); return false; }
        out[n].day = days; out[n].coins = (uint16_t)coins; out[n].cancel = (sc == 'C' || sc == 'c') ? 1 : 0; out[n].box = (uint8_t)box;
        out[n].tod = (uint16_t)(hh * 60 + mi); out[n].durSec = (mins < 0.0f) ? 0xFFFF : (uint16_t)(mins * 60.0f + 0.5f); n++;
    }
    *nOut = n;
    return true;
}
static void histLoad() {                       // (re)read history.json into RAM
    LogGuard g;
    histN = 0; histFromYmd = histToYmd = 0; histMsg[0] = 0; logDirty = true;
    histFin = {0, 0, 0, 0}; histCan = {0, 0, 0, 0};
    if (!logOk || !logFs->exists(HIST_PATH)) return;
    File f = logFs->open(HIST_PATH, "r");
    if (!f) return;
    if (f.size() > HIST_MAX_BYTES) { f.close(); snprintf(histMsg, sizeof(histMsg), "history.json is bigger than %d KB: not used.", HIST_MAX_BYTES / 1024); return; }
    if (!histAlloc()) { f.close(); snprintf(histMsg, sizeof(histMsg), "Not enough memory for the history rows."); return; }
    String txt = f.readString();
    f.close();
    uint16_t n = 0;
    if (!histParseText(txt, hist, &n, histMsg, sizeof(histMsg))) return;
    histN = n;
    for (uint16_t i = 0; i < n; i++) {
        HistTot& T = hist[i].cancel ? histCan : histFin;
        T.n++; T.coins += hist[i].coins;
        if (hist[i].durSec != 0xFFFF) { T.durSec += hist[i].durSec; T.durN++; }
    }
    if (n) {
        int32_t lo = hist[0].day, hi = hist[0].day;
        for (uint16_t i = 1; i < n; i++) { if (hist[i].day < lo) lo = hist[i].day; if (hist[i].day > hi) hi = hist[i].day; }
        histFromYmd = ymdOfDays(lo); histToYmd = ymdOfDays(hi);
    }
    Serial.printf("[LOG] history.json: %u row(s) of previous data, %lu .. %lu\n", (unsigned)histN, (unsigned long)histFromYmd, (unsigned long)histToYmd);
}
static void histClear() {
    LogGuard g;
    if (logOk && logFs->exists(HIST_PATH)) logFs->remove(HIST_PATH);
    histLoad();
}

// upload of a new history.json (the Home page's "Upload"): written to a temp file, checked, then it replaces the old one - a bad file changes nothing
static File histUp;
static size_t histUpBytes = 0;
static bool histUpTooBig = false;
static void histUploadChunk(AsyncWebServerRequest* request, const String& filename, size_t index, uint8_t* data, size_t len, bool final) {
    (void)request; (void)filename;
    if (!logOk) return;
    if (index == 0) { if (histUp) histUp.close(); histUp = logFs->open(HIST_TMP, "w"); histUpBytes = 0; histUpTooBig = false; }
    if (histUp && !histUpTooBig) {
        histUpBytes += len;
        if (histUpBytes > HIST_MAX_BYTES) histUpTooBig = true; else histUp.write(data, len);
    }
    if (final && histUp) histUp.close();
}
static void histUploadDone(AsyncWebServerRequest* request) {
    LogGuard g;
    char err[120] = "";
    bool ok = false;
    if (!logOk) snprintf(err, sizeof(err), "The log storage is not available (partition scheme?).");
    else if (histUpTooBig) snprintf(err, sizeof(err), "The file is bigger than %d KB.", HIST_MAX_BYTES / 1024);
    else if (!histAlloc()) snprintf(err, sizeof(err), "Not enough memory for the history rows.");
    else {
        File f = logFs->open(HIST_TMP, "r");
        String txt = f ? f.readString() : String("");
        if (f) f.close();
        uint16_t n = 0;
        ok = histParseText(txt, hist, &n, err, sizeof(err));
        if (ok) { if (logFs->exists(HIST_PATH)) logFs->remove(HIST_PATH); logFs->rename(HIST_TMP, HIST_PATH); }
    }
    if (logOk && logFs->exists(HIST_TMP)) logFs->remove(HIST_TMP);
    histLoad();                                                      // whatever happened, RAM now matches the file that is stored
    for (char* c = err; *c; ++c) if (*c == '"' || *c == '\\') *c = '\'';        // the text goes into a JSON answer: no quotes or backslashes
    String o = ok ? String("{\"ok\":1,\"rows\":") + String(histN) + ",\"from\":" + String((unsigned long)histFromYmd) + ",\"to\":" + String((unsigned long)histToYmd) + "}"
                  : String("{\"ok\":0,\"msg\":\"") + err + "\"}";
    request->send(200, "application/json", o);
}

// ---------------------------------------------------------------------
// BACKUP RESTORE (see the header). The backup is the /log.csv download: one line per session, plus a last line "#integrity,<sha256>" over all lines above it.
// ---------------------------------------------------------------------
void applyRestoredTotals(uint32_t finN, uint32_t finCoins, uint32_t finDur, uint32_t canN, uint32_t canCoins, uint32_t canDur);      // ESP32_LOCALSRV.ino

#define CSV_HEADER "id,date,time,box,status,coins,money_php,minutes,unix_time,seconds"
#define CSV_SALT   "JollyHelmet-log-v1|"
#define RESTORE_PATH "/restore.csv"
#define RESTORE_MAX_BYTES 2500000UL
static void shaStart(mbedtls_sha256_context* c) { mbedtls_sha256_init(c); mbedtls_sha256_starts(c, 0); mbedtls_sha256_update(c, (const unsigned char*)CSV_SALT, strlen(CSV_SALT)); }
static void shaHex(mbedtls_sha256_context* c, char* hex65) {
    unsigned char out[32];
    mbedtls_sha256_finish(c, out); mbedtls_sha256_free(c);
    for (int i = 0; i < 32; i++) snprintf(hex65 + i * 2, 3, "%02x", out[i]);
}

static File restoreUp;
static size_t restoreUpBytes = 0;
static bool restoreUpTooBig = false;
static volatile bool restorePending = false;    // set by the upload (web server task), taken by logTick() (loop task)
static volatile int8_t restoreState = -1;       // -1 nothing yet, 2 running, 1 done, 0 refused
static char restoreMsg[260] = "";               // the result, written by the loop task only; shown on the Settings card

static bool jLess(const JEnt& a, const JEnt& b) {                       // dated sessions by time, "no date yet" ones last; every field decides so that equal entries end up next to each other
    bool an = (a.flags & JF_NOTIME) != 0, bn = (b.flags & JF_NOTIME) != 0;
    if (an != bn) return bn;
    if (a.t != b.t) return a.t < b.t;
    if (a.box != b.box) return a.box < b.box;
    if (a.coins != b.coins) return a.coins < b.coins;
    if (a.dur != b.dur) return a.dur < b.dur;
    return (a.flags & JF_CANCEL) < (b.flags & JF_CANCEL);
}
static bool jSame(const JEnt& a, const JEnt& b) {
    return !(a.flags & JF_NOTIME) && !(b.flags & JF_NOTIME) && a.t == b.t && a.box == b.box && a.coins == b.coins && a.dur == b.dur && (a.flags & JF_CANCEL) == (b.flags & JF_CANCEL);
}
static void restoreRefuse(const char* why) {
    snprintf(restoreMsg, sizeof(restoreMsg), "NOT restored (the log on the board is untouched). %s", why);
    restoreState = 0;
    Serial.printf("[LOG] restore refused: %s\n", why);
}

static void restoreUploadChunk(AsyncWebServerRequest* request, const String& filename, size_t index, uint8_t* data, size_t len, bool final) {
    (void)request; (void)filename;
    if (!logOk) return;
    if (index == 0) { if (restoreUp) restoreUp.close(); restoreUp = logFs->open(RESTORE_PATH, "w"); restoreUpBytes = 0; restoreUpTooBig = false; }
    if (restoreUp && !restoreUpTooBig) {
        restoreUpBytes += len;
        if (restoreUpBytes > RESTORE_MAX_BYTES) restoreUpTooBig = true; else restoreUp.write(data, len);
    }
    if (final && restoreUp) restoreUp.close();
}
static void restoreUploadDone(AsyncWebServerRequest* request) {                 // only hands the file over: the work runs in loop(), not in the web server task
    const char* err = nullptr;
    if (!logOk) err = "The log storage is not available.";
    else if (restoreUpTooBig) err = "The file is bigger than 2.5 MB: it is not a log backup.";
    else if (restorePending || restoreState == 2) err = "A restore is already running.";
    else if (!logFs->exists(RESTORE_PATH)) err = "The file did not arrive.";
    if (err) {
        if (logOk && logFs->exists(RESTORE_PATH) && !restorePending && restoreState != 2) logFs->remove(RESTORE_PATH);
        request->send(200, "application/json", String("{\"ok\":0,\"msg\":\"") + err + "\"}");
        return;
    }
    restoreState = 2; restorePending = true;
    request->send(200, "application/json", "{\"ok\":1}");
}

static void logRestoreRun() {
    restorePending = false;
    JEnt* all = nullptr;
    auto done = [&]() { if (all) heap_caps_free(all); if (logOk && logFs->exists(RESTORE_PATH)) logFs->remove(RESTORE_PATH); };
    if (!logOk) { restoreRefuse("The log storage is not available."); return; }
    File f = logFs->open(RESTORE_PATH, "r");
    if (!f) { restoreRefuse("The uploaded file could not be read."); return; }
    all = (JEnt*)heap_caps_malloc(sizeof(JEnt) * (size_t)LOG_CAP * 2, MALLOC_CAP_SPIRAM);          // [0, LOG_CAP) = what is on the board, [LOG_CAP, 2 * LOG_CAP) = the backup's rows
    if (!all) { f.close(); done(); restoreRefuse("Not enough memory for a restore (it needs the board's PSRAM)."); return; }
    JEnt* rest = all + LOG_CAP;

    // 1) read the backup (no lock needed: it only touches the uploaded file) and check its integrity line
    mbedtls_sha256_context sha; shaStart(&sha);
    uint32_t R = 0, noDate = 0, lineNo = 0;
    bool sawHeader = false, sawCheck = false;
    char bad[200] = "", want[72] = "";
    while (f.available() && !bad[0]) {
        String ln = f.readStringUntil('\n');
        ln.replace("\r", "");
        lineNo++;
        if (ln.length() == 0) continue;
        if (sawCheck) { snprintf(bad, sizeof(bad), "There is text after the integrity line (line %lu).", (unsigned long)lineNo); break; }
        if (ln.startsWith("#integrity,")) { String h = ln.substring(11); h.trim(); strncpy(want, h.c_str(), sizeof(want) - 1); sawCheck = true; continue; }
        String hl = ln + "\n";
        mbedtls_sha256_update(&sha, (const unsigned char*)hl.c_str(), hl.length());
        if (!sawHeader) {
            if (ln != CSV_HEADER) snprintf(bad, sizeof(bad), "This is not a backup made by this log (the first line is different).");
            sawHeader = true; continue;
        }
        char b[160];
        if (ln.length() >= sizeof(b)) { snprintf(bad, sizeof(bad), "Line %lu is too long.", (unsigned long)lineNo); break; }
        ln.toCharArray(b, sizeof(b));
        char* fld[12]; int nf = 0; char* p = b; fld[nf++] = p;
        for (; *p && nf < 12; p++) if (*p == ',') { *p = 0; fld[nf++] = p + 1; }
        if (nf != 10) { snprintf(bad, sizeof(bad), "Line %lu does not have 10 columns.", (unsigned long)lineNo); break; }
        long box = atol(fld[3]);
        bool can = (strcmp(fld[4], "Cancelled") == 0);
        if (!can && strcmp(fld[4], "Finished") != 0) { snprintf(bad, sizeof(bad), "Line %lu: the status is neither Finished nor Cancelled.", (unsigned long)lineNo); break; }
        if (box < 1 || box > 3) { snprintf(bad, sizeof(bad), "Line %lu: the box must be 1, 2 or 3.", (unsigned long)lineNo); break; }
        unsigned long coins = strtoul(fld[5], nullptr, 10), sec = strtoul(fld[9], nullptr, 10);
        if (coins > 65535UL || sec > 65535UL) { snprintf(bad, sizeof(bad), "Line %lu: a number is too big.", (unsigned long)lineNo); break; }
        if (fld[8][0] == 0) { noDate++; continue; }                                  // a session saved before the clock was set: it cannot be placed in time again
        unsigned long ut = strtoul(fld[8], nullptr, 10);
        if (ut < 1700000000UL || ut > 4102444800UL) { snprintf(bad, sizeof(bad), "Line %lu: the unix time is not a real date.", (unsigned long)lineNo); break; }
        if (R >= LOG_CAP) { snprintf(bad, sizeof(bad), "More than %lu sessions in the file.", (unsigned long)LOG_CAP); break; }
        JEnt e; memset(&e, 0, sizeof(e));
        e.kind = 1; e.box = (uint8_t)box; e.flags = can ? JF_CANCEL : 0; e.t = (uint32_t)ut; e.coins = (uint16_t)coins; e.dur = (uint16_t)sec;
        rest[R++] = e;
        if ((R & 255) == 0) delay(0);                                                // let the radio and the web server breathe
    }
    f.close();
    char got[72] = "";
    shaHex(&sha, got);
    if (!bad[0] && !sawHeader) snprintf(bad, sizeof(bad), "The file is empty.");
    if (!bad[0] && !sawCheck) snprintf(bad, sizeof(bad), "There is no integrity line at the end: only an unchanged download of the log CSV from this firmware can be restored.");
    if (!bad[0] && strcasecmp(want, got) != 0) snprintf(bad, sizeof(bad), "The file was changed after it was downloaded (or saved again in a spreadsheet), so it is not accepted. Corrections go in the JSON file, not here.");
    if (bad[0]) { done(); restoreRefuse(bad); return; }

    // 2) merge with what is on the board, under the log lock so no session can slip in between reading and writing
    LogGuard g;
    uint32_t E = 0;
    jScan([&](uint32_t idx, const JEnt& e) { (void)idx; if (E < LOG_CAP) all[E++] = e; });
    memmove(all + E, rest, (size_t)R * sizeof(JEnt));
    uint32_t total = E + R;
    std::sort(all, all + total, [](const JEnt& a, const JEnt& b) { return jLess(a, b); });
    uint32_t w = 0;
    for (uint32_t i = 0; i < total; i++) {
        if (w > 0 && jSame(all[w - 1], all[i])) continue;                            // the same session twice: keep one
        if (w != i) all[w] = all[i];
        w++;
    }
    if (w > LOG_CAP) { done(); restoreRefuse("Together that would be more sessions than the log can hold."); return; }
    uint32_t added = (w > E) ? (w - E) : 0;

    // 3) write the new file next to the old one, then swap (an interrupted swap is repaired at the next start, see logBegin)
    File o = logFs->open("/sessions.new", "w");
    if (!o) { done(); restoreRefuse("Could not write the new log file."); return; }
    bool okw = true;
    for (uint32_t i = 0; i < w && okw; i += 64) {
        uint32_t n = w - i; if (n > 64) n = 64;
        okw = (o.write((const uint8_t*)&all[i], (size_t)n * sizeof(JEnt)) == (size_t)n * sizeof(JEnt));
        delay(0);
    }
    o.close();
    if (!okw) { logFs->remove("/sessions.new"); done(); restoreRefuse("Writing failed (storage full?). The log on the board is untouched."); return; }
    logFs->remove(LOG_PATH);
    logFs->rename("/sessions.new", LOG_PATH);
    logCount = w; logFull = false; logDirty = true;

    // 4) the totals on the Home page are raised to at least what the log says (never lowered)
    uint32_t fN = 0, fC = 0, fD = 0, cN = 0, cC = 0, cD = 0;
    for (uint32_t i = 0; i < w; i++) {
        if (all[i].flags & JF_CANCEL) { cN++; cC += all[i].coins; cD += all[i].dur; } else { fN++; fC += all[i].coins; fD += all[i].dur; }
    }
    applyRestoredTotals(fN, fC, fD, cN, cC, cD);
    logRefreshFsNumbers();
    snprintf(restoreMsg, sizeof(restoreMsg), "Restored. The backup had %lu sessions: %lu were new and are now in the log, %lu were already there.%s The log now holds %lu sessions. The totals were raised to at least the log's tally.",
             (unsigned long)R, (unsigned long)added, (unsigned long)(R > added ? R - added : 0), noDate ? " (Sessions without a date in the file were skipped.)" : "", (unsigned long)w);
    restoreState = 1;
    Serial.printf("[LOG] restore: %s\n", restoreMsg);
    done();
}
static void logRestoreTick() { if (restorePending) logRestoreRun(); }

// ---------------------------------------------------------------------
// Aggregates for the Home page chart (recomputed only when something changed)
// ---------------------------------------------------------------------
struct DayAgg { uint32_t sess, coins, canSess, canCoins; };
#define AGG_DAYS 100
static DayAgg   aggDay[AGG_DAYS];              // what the MACHINES recorded: [0] = today, [1] = yesterday ...
static DayAgg   aggHist[AGG_DAYS];             // what the team's history file says (previous data), same indexing
static uint32_t aggNoDate = 0;                 // machine sessions that still have no date
static int32_t  aggToday = -1;

static void logRebuildAgg() {
    LogGuard g;
    memset(aggDay, 0, sizeof(aggDay)); memset(aggHist, 0, sizeof(aggHist)); aggNoDate = 0;
    int32_t today = clockIsSet() ? localDayOf(nowEpoch()) : -1;
    aggToday = today;
    jScan([&](uint32_t idx, const JEnt& e) {
        (void)idx;
        if (e.flags & JF_NOTIME) { aggNoDate++; return; }
        if (today < 0) return;
        int32_t k = today - localDayOf(e.t);
        if (k < 0 || k >= AGG_DAYS) return;
        aggDay[k].sess++; aggDay[k].coins += e.coins;
        if (e.flags & JF_CANCEL) { aggDay[k].canSess++; aggDay[k].canCoins += e.coins; }
    });
    if (today >= 0 && hist) {
        for (uint16_t i = 0; i < histN; i++) {
            int32_t k = today - hist[i].day;
            if (k < 0 || k >= AGG_DAYS) continue;
            aggHist[k].sess++; aggHist[k].coins += hist[i].coins;
            if (hist[i].cancel) { aggHist[k].canSess++; aggHist[k].canCoins += hist[i].coins; }
        }
    }
    logDirty = false;
}

static void logFillWs(JsonDocument& doc) {
    doc["clk"] = nowEpoch(); doc["clk_tz"] = tzMin; doc["clk_src"] = clkSrc;
    doc["lg_ok"] = logOk; doc["lg_fs"] = logOk ? (logUsesFat ? "FFat" : "LittleFS") : "none";
    doc["lg_n"] = logCount; doc["lg_cap"] = LOG_CAP; doc["lg_full"] = logFull;
    doc["lg_total"] = fsTotalCached; doc["lg_used"] = fsUsedCached; doc["lg_nodate"] = aggNoDate;
    doc["lg_fmt"] = logFmtCount; doc["lg_fmt_new"] = logReformatted;                    // the storage was damaged and re-created empty (this start / ever)
    doc["lg_rstate"] = (int)restoreState;                                               // backup restore: -1 none yet, 2 running, 1 done, 0 refused
    if (restoreState == 0 || restoreState == 1) doc["lg_rmsg"] = (const char*)restoreMsg;
    doc["hist_n"] = histN; doc["hist_from"] = histFromYmd; doc["hist_to"] = histToYmd; doc["hist_msg"] = histMsg;
    doc["h_fin_n"] = histFin.n; doc["h_fin_c"] = histFin.coins; doc["h_fin_d"] = histFin.durSec; doc["h_fin_dn"] = histFin.durN;      // the JSON file's whole tally, for the Finished / Cancelled card
    doc["h_can_n"] = histCan.n; doc["h_can_c"] = histCan.coins; doc["h_can_d"] = histCan.durSec; doc["h_can_dn"] = histCan.durN;
    if (aggToday < 0) return;
    // every bucket: [yyyymmdd, machine sessions, machine coins, machine cancelled sessions, machine cancelled coins, then the same four from the history file]
    JsonArray d7 = doc.createNestedArray("d7");                       // the last 7 days, oldest first
    for (int k = 6; k >= 0; k--) {
        JsonArray a = d7.createNestedArray();
        a.add(ymdOfDays(aggToday - k)); a.add(aggDay[k].sess); a.add(aggDay[k].coins); a.add(aggDay[k].canSess); a.add(aggDay[k].canCoins);
        a.add(aggHist[k].sess); a.add(aggHist[k].coins); a.add(aggHist[k].canSess); a.add(aggHist[k].canCoins);
    }
    JsonArray w12 = doc.createNestedArray("w12");                     // the last 12 weeks (Monday first), oldest first: [Monday yyyymmdd, ...]
    int32_t dow = (int32_t)(((aggToday + 4) % 7 + 7) % 7);            // 0 = Sunday
    int32_t sinceMon = (dow + 6) % 7;
    for (int w = 11; w >= 0; w--) {
        int32_t monK = sinceMon + 7 * w;
        uint32_t s[2] = {0, 0}, c[2] = {0, 0}, cs[2] = {0, 0}, cc[2] = {0, 0};
        for (int j = 0; j < 7; j++) {
            int32_t k = monK - j; if (k < 0 || k >= AGG_DAYS) continue;
            s[0] += aggDay[k].sess; c[0] += aggDay[k].coins; cs[0] += aggDay[k].canSess; cc[0] += aggDay[k].canCoins;
            s[1] += aggHist[k].sess; c[1] += aggHist[k].coins; cs[1] += aggHist[k].canSess; cc[1] += aggHist[k].canCoins;
        }
        JsonArray a = w12.createNestedArray();
        a.add(ymdOfDays(aggToday - monK)); a.add(s[0]); a.add(c[0]); a.add(cs[0]); a.add(cc[0]); a.add(s[1]); a.add(c[1]); a.add(cs[1]); a.add(cc[1]);
    }
}

// Entries older than a year are dropped (once the clock is set, then once a day). "No date yet" entries are kept. The counters are not touched.
static void logPrune() {
    LogGuard g;
    if (!logOk || !clockIsSet() || logCount == 0) return;
    uint32_t nowE = nowEpoch();
    uint32_t cutoff = nowE > 365UL * 86400UL ? nowE - 365UL * 86400UL : 0;
    uint32_t drop = 0;
    jScan([&](uint32_t idx, const JEnt& e) { (void)idx; if (!(e.flags & JF_NOTIME) && e.t < cutoff) drop++; });
    if (drop == 0) return;
    File in = logFs->open(LOG_PATH, "r");
    File out = logFs->open("/sessions.new", "w");
    if (!in || !out) { if (in) in.close(); if (out) out.close(); return; }
    JEnt buf[32];
    uint32_t kept = 0;
    while (true) {
        size_t n = in.read((uint8_t*)buf, sizeof(buf)) / sizeof(JEnt);
        if (n == 0) break;
        for (size_t i = 0; i < n; i++) {
            if (!(buf[i].flags & JF_NOTIME) && buf[i].t < cutoff) continue;
            out.write((const uint8_t*)&buf[i], sizeof(JEnt)); kept++;
        }
    }
    in.close(); out.close();
    logFs->remove(LOG_PATH);
    logFs->rename("/sessions.new", LOG_PATH);
    logCount = kept; logFull = false; logDirty = true;
    Serial.printf("[LOG] %lu entr%s older than a year removed\n", (unsigned long)drop, drop == 1 ? "y" : "ies");
}

// Once a second from loop()
static void logTick() {
    uint32_t nowMs = millis();
    static uint32_t lastFs = 0, lastPrune = 0;
    static bool pruned = false;
    logRestoreTick();                                                                 // a backup that was just uploaded is merged here (the loop task), not in the web server task
    if (nowMs - lastFs > 10000UL || lastFs == 0) { lastFs = nowMs; logRefreshFsNumbers(); }
    if (clockIsSet() && (!pruned || nowMs - lastPrune > 86400000UL)) { pruned = true; lastPrune = nowMs; logPrune(); }
    if (logDirty || (clockIsSet() && localDayOf(nowEpoch()) != aggToday)) logRebuildAgg();
}

// ---------------------------------------------------------------------
// /log.csv  (streamed: the log can be larger than the free RAM)
// ---------------------------------------------------------------------
static int csvLine(char* out, size_t cap, uint32_t idx, const JEnt& e) {
    char dd[12] = "", tt[8] = "", ut[12] = "";
    if (!(e.flags & JF_NOTIME)) {
        int y, m, d; civilFromDays(localDayOf(e.t), &y, &m, &d);
        snprintf(dd, sizeof(dd), "%04d-%02d-%02d", y, m, d);
        uint32_t s = localSecOfDay(e.t);
        snprintf(tt, sizeof(tt), "%02u:%02u", (unsigned)(s / 3600), (unsigned)((s % 3600) / 60));
        snprintf(ut, sizeof(ut), "%lu", (unsigned long)e.t);                         // the exact time, so a restore is not rounded to the minute
    }
    return snprintf(out, cap, "%u,%s,%s,%u,%s,%u,%.2f,%.1f,%s,%u\n", (unsigned)idx, dd, tt, (unsigned)e.box, (e.flags & JF_CANCEL) ? "Cancelled" : "Finished",
                    (unsigned)e.coins, e.coins * coinValuePeso, e.dur / 60.0, ut, (unsigned)e.dur);
}
static size_t csvFill(uint8_t* buf, size_t maxLen, size_t index) {
    static uint32_t pos = 0;
    static bool headerDone = false, tailDone = false;
    static mbedtls_sha256_context sha;                         // runs over every line that is sent; its result is the last line (#integrity), see the header
    if (index == 0) { pos = 0; headerDone = false; tailDone = false; shaStart(&sha); }
    size_t n = 0;
    if (!headerDone) {
        static const char h[] = CSV_HEADER "\n";
        size_t L = sizeof(h) - 1;
        if (L > maxLen) return 0;
        memcpy(buf, h, L); n = L; headerDone = true;
        mbedtls_sha256_update(&sha, (const unsigned char*)h, L);
    }
    LogGuard g;
    JEnt chunk[16];
    char line[120];
    while (pos < logCount) {
        size_t got = jRead(pos, chunk, 16);
        if (got == 0) { pos = logCount; break; }
        size_t used = 0;
        for (size_t i = 0; i < got; i++) {
            int len = csvLine(line, sizeof(line), pos + (uint32_t)i, chunk[i]);
            if (len <= 0) { used++; continue; }
            if (n + (size_t)len > maxLen) { pos += (uint32_t)used; return n; }       // the rest goes into the next chunk
            memcpy(buf + n, line, (size_t)len); n += (size_t)len; used++;
            mbedtls_sha256_update(&sha, (const unsigned char*)line, (size_t)len);
        }
        pos += (uint32_t)used;
    }
    if (pos >= logCount && !tailDone) {                                               // the last line: the integrity check of everything above
        if (n + 90 > maxLen) return n;                                                // (does not fit in this chunk: it goes into the next one)
        char hex[72]; shaHex(&sha, hex);
        n += (size_t)snprintf((char*)buf + n, maxLen - n, "#integrity,%s\n", hex);
        tailDone = true;
    }
    return n;                                                                         // 0 once everything was sent = end of the file
}
// ---------------------------------------------------------------------
// One day, customer by customer (the Home page's day viewer):  /api/day?d=20261012   (d=0: the machine sessions that still have no date)
//   { "ok":1, "d":20261012, "trunc":0, "rows":[ [secondsOfDay, box, coins, cancelled, durationSec (65535 = not given)], ... ],
//     "hrows":[ [secondsOfDay, box, coins, cancelled, durationSec], ... ] }
//   "rows" = what the MACHINES recorded. "hrows" = what the team's JSON history file (previous data) says for that day. They are two separate lists and the page shows them
//   on two separate tabs, each with its own name; they are never mixed into one list.
// ---------------------------------------------------------------------
struct DayLine { uint32_t sod; uint16_t coins, durSec; uint8_t box, cancel, src; };
#define DAY_MAX 200
static DayLine dayLines[DAY_MAX];                // the machine sessions of the day
static DayLine histLines[DAY_MAX];               // the JSON-file customers of the day
static void dayPut(DayLine* arr, uint16_t* n, bool* trunc, uint32_t sod, uint16_t coins, uint16_t dur, uint8_t box, uint8_t cancel, uint8_t src) {
    if (*n >= DAY_MAX) { *trunc = true; return; }
    uint16_t p = *n;
    while (p > 0 && arr[p - 1].sod > sod) { arr[p] = arr[p - 1]; p--; }
    arr[p].sod = sod; arr[p].coins = coins; arr[p].durSec = dur; arr[p].box = box; arr[p].cancel = cancel; arr[p].src = src;
    (*n)++;
}
static void dayAppend(String& o, const DayLine* arr, uint16_t n) {
    for (uint16_t i = 0; i < n; i++) {
        const DayLine& L = arr[i];
        if (i) o += ",";
        o += "["; o += String((unsigned long)L.sod); o += ","; o += String((unsigned)L.box); o += ","; o += String((unsigned)L.coins); o += ",";
        o += String((unsigned)L.cancel); o += ","; o += String((unsigned)L.durSec); o += "]";
    }
}
static void logDayJson(uint32_t ymd, String& o) {
    LogGuard g;
    bool nodate = (ymd == 0);
    int32_t dn = 0;
    if (!nodate && !daysOfYmdParts((int)(ymd / 10000), (int)((ymd / 100) % 100), (int)(ymd % 100), &dn)) { o = "{\"ok\":0,\"msg\":\"not a real date\"}"; return; }
    uint16_t n = 0, hn = 0; bool trunc = false;
    jScan([&](uint32_t idx, const JEnt& e) {
        (void)idx;
        bool nt = (e.flags & JF_NOTIME) != 0;
        if (nodate ? !nt : (nt || localDayOf(e.t) != dn)) return;
        dayPut(dayLines, &n, &trunc, nt ? e.t : localSecOfDay(e.t), e.coins, e.dur, e.box, (e.flags & JF_CANCEL) ? 1 : 0, 0);
    });
    if (!nodate && hist) {
        for (uint16_t i = 0; i < histN; i++) if (hist[i].day == dn) dayPut(histLines, &hn, &trunc, (uint32_t)hist[i].tod * 60UL, hist[i].coins, hist[i].durSec, hist[i].box, hist[i].cancel, 1);
    }
    o = "";
    o.reserve(100 + ((size_t)n + hn) * 32);
    o += "{\"ok\":1,\"d\":"; o += String((unsigned long)ymd); o += ",\"trunc\":"; o += trunc ? "1" : "0"; o += ",\"rows\":[";
    dayAppend(o, dayLines, n);
    o += "],\"hrows\":[";
    dayAppend(o, histLines, hn);
    o += "]}";
}

static void logRegisterRoutes() {
    server.on("/api/day", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (!request) return;
        uint32_t ymd = request->hasParam("d") ? (uint32_t)strtoul(request->getParam("d")->value().c_str(), nullptr, 10) : 0;
        String o;
        logDayJson(ymd, o);
        AsyncWebServerResponse* res = request->beginResponse(200, "application/json", o);
        res->addHeader("Cache-Control", "no-store");
        request->send(res);
    });
    server.on("/history.json", HTTP_GET, [](AsyncWebServerRequest* request) {            // the team's history file (or an empty template to start from)
        if (!request) return;
        if (logOk && logFs->exists(HIST_PATH)) {
            AsyncWebServerResponse* res = request->beginResponse(*logFs, HIST_PATH, "application/json");
            res->addHeader("Content-Disposition", "attachment; filename=\"history.json\"");
            request->send(res);
        } else {
            AsyncWebServerResponse* res = request->beginResponse(200, "application/json",
                "{\n \"source\": \"previous data collected after the upgrade\",\n \"format\": \"rows: [date YYYY-MM-DD, time HH:MM, box 1-3, coins, F finished or C cancelled, minutes (optional)]. One row per customer, never a name.\",\n \"rows\": [\n ]\n}\n");
            res->addHeader("Content-Disposition", "attachment; filename=\"history.json\"");
            request->send(res);
        }
    });
    server.on("/history/upload", HTTP_POST, histUploadDone, histUploadChunk);
    server.on("/log/restore", HTTP_POST, restoreUploadDone, restoreUploadChunk);          // Settings > Restore from backup CSV
    server.on("/log.csv", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (!request) return;
        AsyncWebServerResponse* res = request->beginChunkedResponse("text/csv", csvFill);
        res->addHeader("Content-Disposition", "attachment; filename=\"JollyHelmet_session_log.csv\"");
        res->addHeader("Cache-Control", "no-store");
        request->send(res);
    });
}
