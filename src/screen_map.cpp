#include <Arduino.h>
#include <TFT_eSPI.h>
#include <TinyGPSPlus.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

#include "screens.h"
#include "screen_map.h"
#include "config.h"

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

    if (requiresInit) {
        // fetch raw rgb565 from backend
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

        String url = String(BASE_URL) + String("/map/device.rgb565?width=320&height=240");

        if (http.begin(client, url)) {
            int code = http.GET();
            if (code == HTTP_CODE_OK) {
                int len = http.getSize();
                int expected = W * H * 2;
                if (len == expected) {
                    uint8_t *buf = (uint8_t*)malloc(len);
                    if (buf) {
                        WiFiClient *stream = http.getStreamPtr();
                        int read = 0;
                        while (read < len) {
                            int r = stream->readBytes((char*)buf + read, len - read);
                            if (r <= 0) break;
                            read += r;
                        }

                        if (read == len) {
                            // interpret buffer as little-endian uint16_t array
                            uint16_t *pixels = (uint16_t*)buf;
                            tft.setSwapBytes(false); // device little-endian
                            tft.pushImage(0, 0, W, H, pixels);
                            // free buffer (pushImage copies data)
                            free(buf);
                        } else {
                            free(buf);
                            tft.fillScreen(TFT_BLACK);
                            tft.setTextDatum(MC_DATUM);
                            tft.setTextColor(TFT_RED, TFT_BLACK);
                            tft.drawString("Map read error", 160, 120, 4);
                        }
                    }
                } else {
                    tft.fillScreen(TFT_BLACK);
                    tft.setTextDatum(MC_DATUM);
                    tft.setTextColor(TFT_RED, TFT_BLACK);
                    tft.drawString("Bad map size", 160, 120, 4);
                }
            } else {
                tft.fillScreen(TFT_BLACK);
                tft.setTextDatum(MC_DATUM);
                tft.setTextColor(TFT_RED, TFT_BLACK);
                tft.drawString("Map fetch fail", 160, 120, 4);
            }
            http.end();
        } else {
            tft.fillScreen(TFT_BLACK);
            tft.setTextDatum(MC_DATUM);
            tft.setTextColor(TFT_RED, TFT_BLACK);
            tft.drawString("HTTP init fail", 160, 120, 4);
        }

        mapLoading = false;
        return;
    }

    // simple static display while not reloading: draw last known status
    if (mapLoading) {
        drawPlaceholder();
    } else {
        // small indicator
        tft.setTextDatum(TR_DATUM);
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.drawString("Map", 315, 5, 2);
    }
}

void screenMapButton(Button button, ButtonEvent event)
{
    if (button == Button::Left && event == ButtonEvent::ShortPress) {
        nextScreen();
    }
}
