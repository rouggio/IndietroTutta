#include "gps_debug.h"

String nmeaLines[MAX_NMEA_LINES];

static String currentLine;
static int nextLine = 0;
static bool collecting = false;

void processNMEAChar(char c)
{
if (c == '$')
    {
        currentLine = "$";
        collecting = true;
        return;
    }

    if (!collecting)
        return;

    if (c == '\r')
        return;

    if (c == '\n')
    {
        nmeaLines[nextLine] = currentLine;
        nextLine = (nextLine + 1) % MAX_NMEA_LINES;
        currentLine = "";
        collecting = false;
        return;
    }

    currentLine += c;
}

int getNextNMEALine()
{
    return nextLine;
}