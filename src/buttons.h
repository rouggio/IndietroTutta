#pragma once

enum class Button {
    Left,
    Right,
    Third   // v2 only (PIN_BTN_THIRD); no UI action bound yet
};

enum class ButtonEvent {
    Press,
    Release,
    ShortPress,
    LongPress
};

using ButtonCallback = void (*)(Button button, ButtonEvent event);

void buttonsInit();
void buttonsUpdate();
void buttonsSetCallback(ButtonCallback callback);