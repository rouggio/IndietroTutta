#include "buttons.h"
#include "board_pins.h"
#include "sound.h"
#include <Arduino.h>

struct ButtonState {
    int pin;
    bool pressed;
    bool longPressDetected;
    unsigned long pressStart;
};

// No debounce timer: the edge test is (level changed since the last poll) and the
// loop runs every few ms, so a real bounce can at most tick `soundButtonPress()`
// twice — the old DEBOUNCE_MS/lastDebounce pair was never read.
static const unsigned long LONG_PRESS_MS = 500;

// Order MUST match the Button enum (the index is cast back to Button).
// PIN_BTN_THIRD is negative on v1, where the third button does not exist.
static ButtonState buttons[] = {
    { .pin = PIN_BTN_LEFT,  .pressed = false, .longPressDetected = false, .pressStart = 0 }, // Left
    { .pin = PIN_BTN_RIGHT, .pressed = false, .longPressDetected = false, .pressStart = 0 }, // Right
#if PIN_BTN_THIRD >= 0
    { .pin = PIN_BTN_THIRD, .pressed = false, .longPressDetected = false, .pressStart = 0 }, // Third
#endif
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

            // Press feedback: fires once on the edge, not while held, so
            // holding for a long press does not repeat the tick.
            soundButtonPress();

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