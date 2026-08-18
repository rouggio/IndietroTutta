#include "serial_buffer.h"

static const int MAX_LINES = 200;
static const int MAX_LINE_LEN = 256;

static String lines[MAX_LINES];
static int lineCount = 0;
static String curLine = "";

void pushLine(const String &s) {
    if (s.length() == 0) return;
    if (lineCount == MAX_LINES) {
        // drop oldest
        for (int i = 1; i < MAX_LINES; ++i) lines[i-1] = lines[i];
        lines[MAX_LINES-1] = s;
    } else {
        lines[lineCount++] = s;
    }
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
int serialLinesCount() { return lineCount; }

String serialLine(int index) {
    if (index < 0) return String();
    if (index >= lineCount) return String();
    return lines[index];
}

void clearSerialBuffer() {
    for (int i = 0; i < lineCount; ++i) lines[i] = String();
    lineCount = 0;
    curLine = String();
}

// Push a line programmatically into the buffer
void serialBufferPush(const String &s) {
    String copy = s;
    if (copy.length() > MAX_LINE_LEN) copy = copy.substring(0, MAX_LINE_LEN);
    pushLine(copy);
}

// Helper that prints and also captures
void bufferedSerialPrintln(const String &s) {
    Serial.println(s);
    serialBufferPush(s);
}

void bufferedSerialPrintln(const char *s) {
    if (s) {
        Serial.println(s);
        serialBufferPush(String(s));
    } else {
        Serial.println();
        serialBufferPush(String());
    }
}

void bufferedSerialPrint(const String &s) {
    Serial.print(s);
    serialBufferPush(s);
}

void bufferedSerialPrint(const char *s) {
    if (s) {
        Serial.print(s);
        serialBufferPush(String(s));
    }
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
