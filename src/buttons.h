#pragma once

enum class Button {
    Left,
    Right
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