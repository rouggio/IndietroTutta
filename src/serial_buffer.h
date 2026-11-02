#pragma once

#include <Arduino.h>

void serialBufferLoop();
int serialLinesCount();
String serialLine(int index); // 0 = oldest, count-1 = newest
void clearSerialBuffer();

// Push a line into the serial buffer (does not print to UART)
void serialBufferPush(const String &s);

// Print to Serial and also capture into the buffer
void bufferedSerialPrintln(const String &s);
void bufferedSerialPrintln(const char *s);

// Print without newline and capture
void bufferedSerialPrint(const String &s);
void bufferedSerialPrint(const char *s);

// printf-style formatted print (newline not appended unless format contains it)
void bufferedSerialPrintf(const char *fmt, ...);

// Replay the tail of the PREVIOUS boot's log into the ring, prefixed [PREV].
// Call once, early in setup(). Returns the number of lines recovered.
int serialBufferReplayRtcLog();