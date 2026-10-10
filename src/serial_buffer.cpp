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

void pushLine(const String &s) {
    if (s.length() == 0) return;
    portENTER_CRITICAL(&bufMux);
    if (lineCount == MAX_LINES) {
        // drop oldest
        for (int i = 1; i < MAX_LINES; ++i) lines[i-1] = lines[i];
        lines[MAX_LINES-1] = s;
    } else {
        lines[lineCount++] = s;
    }
    portEXIT_CRITICAL(&bufMux);
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
