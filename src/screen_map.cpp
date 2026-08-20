#include <Arduino.h>
#include <TFT_eSPI.h>
#include <TinyGPSPlus.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

#include "screens.h"
#include "screen_map.h"
#include "config.h"
#include "serial_buffer.h"

extern TFT_eSPI tft;

static bool mapLoading = false;

static void drawPlaceholder() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("Loading map...", 160, 120, 4);
}

void drawScreenMap(TinyGPSPlus &gps, bool requiresInit)
{
    const int W = 320;
    const int H = 240;
    const int ROWS_PER_CHUNK = 10;
    const int CHUNK_SIZE = W * ROWS_PER_CHUNK * 2;

    if (requiresInit) {

        if (WiFi.status() != WL_CONNECTED) {
            tft.fillScreen(TFT_BLACK);
            tft.setTextDatum(MC_DATUM);
            tft.setTextColor(TFT_RED, TFT_BLACK);
            tft.drawString("No WiFi", 160, 120, 4);
            return;
        }

        drawPlaceholder();
        mapLoading = true;

        WiFiClientSecure client;
        client.setInsecure();

        HTTPClient http;

        String url = String(BASE_URL) +
                     "/map/device.rgb565?width=320&height=240";

        bufferedSerialPrintf("[Map] URL: %s\n", url.c_str());

        if (!http.begin(client, url)) {
            bufferedSerialPrintf("[Map] HTTP init failed\n");

            tft.fillScreen(TFT_BLACK);
            tft.setTextDatum(MC_DATUM);
            tft.setTextColor(TFT_RED, TFT_BLACK);
            tft.drawString("HTTP init fail", 160, 120, 4);

            mapLoading = false;
            return;
        }

        int code = http.GET();

        bufferedSerialPrintf("[Map] HTTP code: %d\n", code);

        if (code != HTTP_CODE_OK) {

            tft.fillScreen(TFT_BLACK);
            tft.setTextDatum(MC_DATUM);
            tft.setTextColor(TFT_RED, TFT_BLACK);
            tft.drawString("Map fetch fail", 160, 100, 4);

            tft.setTextColor(TFT_WHITE, TFT_BLACK);
            tft.drawString(String(code), 160, 140, 4);

            http.end();
            mapLoading = false;
            return;
        }

        WiFiClient *stream = http.getStreamPtr();

        uint8_t *buffer = (uint8_t *)malloc(CHUNK_SIZE);

        if (!buffer) {

            bufferedSerialPrintf(
                "[Map] Cannot allocate %d bytes\n",
                CHUNK_SIZE
            );

            tft.fillScreen(TFT_BLACK);
            tft.setTextDatum(MC_DATUM);
            tft.setTextColor(TFT_RED, TFT_BLACK);
            tft.drawString("No mem for map", 160, 120, 4);

            http.end();
            mapLoading = false;
            return;
        }

        const int expectedBytes = W * H * 2;
        int totalRead = 0;

        unsigned long lastData = millis();
        const unsigned long timeout = 5000;

        tft.setSwapBytes(false);

        while (totalRead < expectedBytes) {

            int rows = min(
                ROWS_PER_CHUNK,
                H - (totalRead / (W * 2))
            );

            int bytesToRead = W * rows * 2;
            int chunkRead = 0;

            while (chunkRead < bytesToRead) {

                if (stream->available()) {

                    int available = stream->available();

                    int remaining = bytesToRead - chunkRead;

                    int toRead = min(available, remaining);

                    int r = stream->readBytes(
                        (char *)buffer + chunkRead,
                        toRead
                    );

                    if (r > 0) {
                        chunkRead += r;
                        totalRead += r;
                        lastData = millis();
                    }
                }
                else {
                    delay(5);
                }

                if (millis() - lastData > timeout) {
                    bufferedSerialPrintf(
                        "[Map] Timeout: %d/%d bytes\n",
                        totalRead,
                        expectedBytes
                    );

                    free(buffer);
                    http.end();

                    tft.fillScreen(TFT_BLACK);
                    tft.setTextDatum(MC_DATUM);
                    tft.setTextColor(TFT_RED, TFT_BLACK);
                    tft.drawString("Map timeout", 160, 120, 4);

                    mapLoading = false;
                    return;
                }
            }

            int y = (totalRead - chunkRead) / (W * 2);

            tft.pushImage(
                0,
                y,
                W,
                rows,
                (uint16_t *)buffer
            );
        }

        bufferedSerialPrintf(
            "[Map] Received %d bytes\n",
            totalRead
        );

        free(buffer);
        http.end();

        mapLoading = false;

        return;
    }

    // No reload: keep the already displayed map.
    if (mapLoading) {
        drawPlaceholder();
    }
}

void screenMapButton(Button button, ButtonEvent event)
{
    // Left short returns to main screen
    if (button == Button::Left && event == ButtonEvent::ShortPress) {
        setCurrentPage(0);
        return;
    }

    // fallback: right short cycles
    if (button == Button::Right && event == ButtonEvent::ShortPress) {
        nextScreen();
    }
}
