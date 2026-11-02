#include "serial_buffer.h"
#include "race_session.h"

#include <freertos/FreeRTOS.h>

// The backend worker task logs from a different thread than the main
// loop, so buffer access is guarded by a short critical section.
static portMUX_TYPE bufMux = portMUX_INITIALIZER_UNLOCKED;

static const int MAX_LINES = 200;
static const int MAX_LINE_LEN = 256;

static String lines[MAX_LINES];
static int lineCount = 0;
static String curLine = "";

// ---------------------------------------------------------
// A tail of the log kept in RTC memory.
//
// A reboot used to leave NO evidence: the ring above lives in heap, so the
// very reset we need to explain wipes it, and the ESP-IDF panic backtrace
// goes to the UART only - not to this ring, so /serial cannot show it.
// RTC RAM survives a panic, a watchdog and ESP.restart() (only a power cut
// loses it), so the last ~1KB of lines is mirrored there and replayed into
// the ring at the next boot as "[PREV] ...".
//
// Same reasoning as a flight recorder: the value is in what was written just
// BEFORE the event, which is exactly what a heap buffer cannot keep.
// ---------------------------------------------------------
#define RTC_LOG_BYTES 1024
#define RTC_LOG_LINE_MAX 96
#define RTC_LOG_MAGIC 0x52544C47u // "RTLG"

RTC_DATA_ATTR static uint32_t rtcLogMagic = 0;
RTC_DATA_ATTR static uint32_t rtcLogHead = 0;   // next write index
RTC_DATA_ATTR static uint32_t rtcLogUsed = 0;   // bytes in use
RTC_DATA_ATTR static char rtcLogBuf[RTC_LOG_BYTES];

// Caller holds bufMux. Newlines are appended per line, and a line longer than
// RTC_LOG_LINE_MAX is truncated: 1KB has to hold a useful NUMBER of lines, not
// one perfect one.
static void rtcLogAppend(const String &s)
{
    if (s.length() == 0) return;
    if (rtcLogMagic != RTC_LOG_MAGIC) { // first write since a power-on
        rtcLogMagic = RTC_LOG_MAGIC;
        rtcLogHead = 0;
        rtcLogUsed = 0;
    }
    const unsigned n = s.length() < RTC_LOG_LINE_MAX ? s.length() : RTC_LOG_LINE_MAX;
    for (unsigned i = 0; i < n; i++) {
        rtcLogBuf[rtcLogHead] = s[i];
        rtcLogHead = (rtcLogHead + 1) % RTC_LOG_BYTES;
        if (rtcLogUsed < RTC_LOG_BYTES) rtcLogUsed++;
    }
    rtcLogBuf[rtcLogHead] = '\n';
    rtcLogHead = (rtcLogHead + 1) % RTC_LOG_BYTES;
    if (rtcLogUsed < RTC_LOG_BYTES) rtcLogUsed++;
}

static void pushLineEx(const String &s, bool mirror);

void pushLine(const String &s) { pushLineEx(s, true); }

static void pushLineEx(const String &s, bool mirror) {
    if (s.length() == 0) return;
    portENTER_CRITICAL(&bufMux);
    if (lineCount == MAX_LINES) {
        // drop oldest
        for (int i = 1; i < MAX_LINES; ++i) lines[i-1] = lines[i];
        lines[MAX_LINES-1] = s;
    } else {
        lines[lineCount++] = s;
    }
    if (mirror) rtcLogAppend(s);
    portEXIT_CRITICAL(&bufMux);
}

int serialBufferReplayRtcLog()
{
    if (rtcLogMagic != RTC_LOG_MAGIC || rtcLogUsed == 0) return 0;
    int count = 0;
    String cur = "";
    for (uint32_t i = 0; i < rtcLogUsed; i++) {
        const uint32_t idx = (rtcLogHead + RTC_LOG_BYTES - rtcLogUsed + i) % RTC_LOG_BYTES;
        const char c = rtcLogBuf[idx];
        if (c == '\n') {
            if (cur.length()) {
                pushLineEx(String("[PREV] ") + cur, false);
                count++;
            }
            cur = "";
        } else {
            cur += c;
        }
    }
    if (cur.length()) { // tail without a newline (the crash mid-line)
        pushLineEx(String("[PREV] ") + cur, false);
        count++;
    }
    // Consumed: the next reboot should show the NEW boot's tail, not this one.
    rtcLogUsed = 0;
    rtcLogHead = 0;
    rtcLogMagic = 0;
    return count;
}

void serialBufferLoop() {
    while (Serial.available() > 0) {
        char c = (char)Serial.read();
        if (c == '\r') continue; // ignore CR
        if (c == '\n') {
            String s = curLine;
            if (s.length() > MAX_LINE_LEN) s = s.substring(0, MAX_LINE_LEN);
            pushLine(s);
            curLine = "";
        } else {
            curLine += c;
            if (curLine.length() > MAX_LINE_LEN) {
                // flush if too long
                pushLine(curLine.substring(0, MAX_LINE_LEN));
                curLine = "";
            }
        }
    }
}

// Expose current buffer state
int serialLinesCount() {
    portENTER_CRITICAL(&bufMux);
    int c = lineCount;
    portEXIT_CRITICAL(&bufMux);
    return c;
}

String serialLine(int index) {
    if (index < 0) return String();
    portENTER_CRITICAL(&bufMux);
    String s = (index < lineCount) ? lines[index] : String();
    portEXIT_CRITICAL(&bufMux);
    return s;
}

void clearSerialBuffer() {
    portENTER_CRITICAL(&bufMux);
    for (int i = 0; i < lineCount; ++i) lines[i] = String();
    lineCount = 0;
    curLine = String();
    portEXIT_CRITICAL(&bufMux);
}

// Push a line programmatically into the buffer
void serialBufferPush(const String &s) {
    String copy = s;
    if (copy.length() > MAX_LINE_LEN) copy = copy.substring(0, MAX_LINE_LEN);
    pushLine(copy);
}

// Log timestamp: wall clock as [DD/MM/YYYY HH:MM:SSZ] when the GPS-side
// clock is calibrated — the Z is the point: device time is always UTC, so
// a stamped line can never be mistaken for local time again. Before
// calibration, boot uptime ([+SSS s], no zone by design).
static bool atLineStart = true;

// Howard Hinnant's civil_from_days: days since 1970-01-01 → y/m/d.
static void civilFromDays(long z, int &y, unsigned &mo, unsigned &d)
{
    z += 719468;
    const long era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = (int)yoe + (int)(era * 400);
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    mo = mp + (mp < 10 ? 3 : 9 - 12);
    y += (mo <= 2);
}

static String logStamp()
{
    char b[32];
    const long wall = raceWallEpoch();
    if (wall > 0) {
        const long days = wall / 86400L;
        const long tod = wall % 86400L;
        int y;
        unsigned mo, d;
        civilFromDays(days, y, mo, d);
        snprintf(b, sizeof(b), "[%02u/%02u/%04d %02ld:%02ld:%02ldZ] ",
                 d, mo, y, tod / 3600, (tod % 3600) / 60, tod % 60);
    } else {
        snprintf(b, sizeof(b), "[+%lus] ", (unsigned long)(millis() / 1000));
    }
    return String(b);
}

// Helper that prints and also captures
void bufferedSerialPrintln(const String &s) {
    const String line = atLineStart ? logStamp() + s : s;
    atLineStart = true;
    Serial.println(line);
    serialBufferPush(line);
}

void bufferedSerialPrintln(const char *s) {
    bufferedSerialPrintln(s ? String(s) : String());
}

void bufferedSerialPrint(const String &s) {
    const String chunk = atLineStart ? logStamp() + s : s;
    atLineStart = false;
    Serial.print(chunk);
    serialBufferPush(chunk);
}

void bufferedSerialPrint(const char *s) {
    bufferedSerialPrint(s ? String(s) : String());
}

#include <stdarg.h>

void bufferedSerialPrintf(const char *fmt, ...) {
    char tmp[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    Serial.printf("%s", tmp);
    serialBufferPush(String(tmp));
}
