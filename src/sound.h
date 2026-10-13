#pragma once

#include <Arduino.h>
#include <stddef.h>

// --------------------------------------------------
// Buzzer sound scripts (test hook).
//
// A script is a list of [freqHz, durationMs] notes; freq 0 = silence.
//
// Playback is delegated to the Arduino core's own tone machinery: on
// ESP32 core 2.0.17 `tone()` does NOT drive the pin inline — it
// xQueueSend()s to a 128-entry queue drained by a `toneTask` the core
// creates on first use, and THAT task does the delay() (see
// cores/esp32/Tone.cpp). So a tune is just a burst of tone() calls with
// no delay and no loop of our own, and none of it can stall loop():
// the UI, buttons, GPS and the HTTP server all keep running.
//
// Consequences of that design, which this API reflects:
//   * "replace" is NOT possible. The queue is FIFO and _tone_queue is
//     static inside Tone.cpp, so there is no way to reset it from
//     outside. A new script queues BEHIND whatever is still pending.
//     SOUND_MAX_TOTAL_MS bounds how long that wait can be.
//   * the queue holds 128 messages; SOUND_MAX_NOTES stays well under
//     that, because xQueueSend uses portMAX_DELAY and a full queue
//     WOULD block the caller.
//   * timing lives in the core task, so there is no local playback
//     progress to observe. soundPlaying() is an estimate from when we
//     queued, and ignores any script queued ahead of it.
// --------------------------------------------------

// Passive buzzer: + on this pin, - on GND.
#define SOUND_PIN 19

#define SOUND_MAX_NOTES 64
#define SOUND_MAX_NOTE_MS 5000
#define SOUND_MAX_TOTAL_MS 10000

// Copy and validate a JSON `notes` array into `out`.
// Returns the note count, or -1 with `err` set to a static message.
// Range-checks only: non-numeric JSON values degrade to 0 (a silent
// note), which is harmless for a test hook.
int soundParseNotes(const char *json, uint16_t (*out)[2], size_t maxNotes,
                    const char **err);

// Queue a whole script. Returns false if the script is empty or too big.
bool soundPlay(const uint16_t (*notes)[2], size_t count);

// Boot chime, played once from setup(). Same notes as bruno/sound-arpeggio.bru
// so what you audition on the portal is what the boat does at power-up.
void soundStartup();

// One short tick per physical button press (press edge only, never during
// a hold). Fired from buttons.cpp, so the remote POST /btn injection —
// which goes straight to screenButtonEvent — does not tick.
// Silently does nothing while the beepOnKeys setting is off.
void soundButtonPress();

// Estimate: true while a script we queued should still be sounding.
bool soundPlaying();
