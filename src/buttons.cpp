#include "buttons.h"
#include <Arduino.h>

struct ButtonState {
    int pin;
    bool pressed;
    bool longPressDetected;
    unsigned long pressStart;
    unsigned long lastDebounce;
};

static const unsigned long DEBOUNCE_MS = 50;
static const unsigned long LONG_PRESS_MS = 500;

static ButtonState buttons[] = {
    { .pin = 21, .pressed = false, .longPressDetected = false, .pressStart = 0, .lastDebounce = 0 }, // Left
    { .pin = 22, .pressed = false, .longPressDetected = false, .pressStart = 0, .lastDebounce = 0 }  // Right
};

static const int NUM_BUTTONS =
    sizeof(buttons) / sizeof(buttons[0]);

static ButtonCallback callback = nullptr;

void buttonsSetCallback(ButtonCallback cb) {
    callback = cb;
}

void buttonsInit() {

    for (int i = 0; i < NUM_BUTTONS; i++) {
        pinMode(buttons[i].pin, INPUT_PULLUP);
    }
}

void buttonsUpdate() {

    for (int i = 0; i < NUM_BUTTONS; i++) {

        ButtonState &button = buttons[i];

        bool nowPressed = digitalRead(button.pin) == LOW;

        // Button just pressed
        if (nowPressed && !button.pressed) {

            button.pressed = true;
            button.longPressDetected = false;
            button.pressStart = millis();

            if (callback) {
                callback(
                    static_cast<Button>(i),
                    ButtonEvent::Press
                );
            }
        }

        // Long press
        if (nowPressed &&
            button.pressed &&
            !button.longPressDetected) {

            if (millis() - button.pressStart >= LONG_PRESS_MS) {

                button.longPressDetected = true;

                if (callback) {
                    callback(
                        static_cast<Button>(i),
                        ButtonEvent::LongPress
                    );
                }
            }
        }

        // Button released
        if (!nowPressed && button.pressed) {

            button.pressed = false;

            if (callback) {
                callback(
                    static_cast<Button>(i),
                    ButtonEvent::Release
                );
            }

            // Only generate ShortPress if LongPress wasn't triggered
            if (!button.longPressDetected) {

                if (callback) {
                    callback(
                        static_cast<Button>(i),
                        ButtonEvent::ShortPress
                    );
                }
            }
        }
    }
}