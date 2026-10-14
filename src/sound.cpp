#include "sound.h"
#include "config_store.h"
#include "serial_buffer.h"
#include <ArduinoJson.h>

// Local estimate of playback progress — the core tone task owns the real
// timing and exposes nothing back to us.
static unsigned long busyUntilMs = 0;

int soundParseNotes(const char *json, uint16_t (*out)[2], size_t maxNotes,
                    const char **err)
{
    if (err) *err = nullptr;
    if (!json || !*json) {
        if (err) *err = "empty body";
        return -1;
    }

    DynamicJsonDocument doc(3072);
    DeserializationError de = deserializeJson(doc, json);
    if (de) {
        if (err) *err = "bad json";
        return -1;
    }

    // ArduinoJson 6 has no is<JsonArray>(); as<JsonArray>() yields a
    // null/empty array when the value is not one, so the size check below
    // covers both "wrong type" and "too short".
    JsonArray arr = doc["notes"].as<JsonArray>();
    if (doc["notes"].isNull() || arr.size() == 0) {
        if (err) *err = "notes array missing or empty";
        return -1;
    }
    if (arr.size() > (int)maxNotes) {
        if (err) *err = "too many notes";
        return -1;
    }

    long total = 0;
    size_t idx = 0;
    for (JsonVariant v : arr) {
        JsonArray note = v.as<JsonArray>();
        if (note.size() < 2) {
            if (err) *err = "each note must be [freqHz, durationMs]";
            return -1;
        }
        long freq = note[0].as<long>();
        long dur = note[1].as<long>();
        // freq 0 is a silence gap; above 20k nothing is audible and it
        // would only waste the pin.
        if (freq < 0 || freq > 20000) {
            if (err) *err = "freq must be 0..20000 Hz";
            return -1;
        }
        if (dur < 1 || dur > SOUND_MAX_NOTE_MS) {
            if (err) *err = "duration must be 1..5000 ms";
            return -1;
        }
        total += dur;
        if (total > SOUND_MAX_TOTAL_MS) {
            if (err) *err = "script too long (max 10s)";
            return -1;
        }
        out[idx][0] = (uint16_t)freq;
        out[idx][1] = (uint16_t)dur;
        idx++;
    }
    return (int)idx;
}

bool soundPlay(const uint16_t (*notes)[2], size_t count)
{
    if (!notes || count == 0 || count > SOUND_MAX_NOTES) return false;

    // The whole tune in one burst: every tone() call enqueues and returns
    // immediately, the core task plays them back to back.
    for (size_t i = 0; i < count; i++) {
        tone(SOUND_PIN, notes[i][0], notes[i][1]);
    }

    unsigned long totalMs = 0;
    for (size_t i = 0; i < count; i++) totalMs += notes[i][1];
    busyUntilMs = millis() + totalMs;
    return true;
}

bool soundPlaying()
{
    return (long)(millis() - busyUntilMs) < 0;
}

// Boot chime. Pitches rise 880 -> 1760 Hz, then a closing beat and a
// short silence. Queued, not blocked: the core tone task plays it while
// setup() carries straight on to the splash, so boot is not held for
// half a second of audio.
static const uint16_t startupNotes[][2] = {
    { 880, 75 },
    { 1109, 75 },
    { 1319, 75 },
    { 1760, 200 },
    { 0, 100 },
};

void soundStartup()
{
    if (!bootSoundOn) return;
    soundPlay(startupNotes, sizeof(startupNotes) / sizeof(startupNotes[0]));
}

// Press feedback: one crisp 25ms blip. Short enough not to be heard as
// a tone, long enough to register as "got it".
static const uint16_t pressNotes[][2] = {
    { 2500, 25 },
};

void soundButtonPress()
{
    // Gated here rather than at the call site so every press feedback path
    // obeys the same setting. Note the remote POST /btn route never reaches
    // this function — it injects straight into screenButtonEvent.
    if (!beepOnKeys) return;
    soundPlay(pressNotes, sizeof(pressNotes) / sizeof(pressNotes[0]));
}

void soundGun(bool ocs)
{
    if (!gunSoundOn) return;

    static const uint16_t goNotes[][2] = { { 880, 70 }, { 0, 25 }, { 1320, 70 } };
    static const uint16_t ocsNotes[][2] = { { 440, 80 }, { 0, 40 }, { 220, 120 } };

    bufferedSerialPrintln(ocs ? "[SOUND] gun OCS" : "[SOUND] gun GO");
    if (ocs) {
        soundPlay(ocsNotes, sizeof(ocsNotes) / sizeof(ocsNotes[0]));
    } else {
        soundPlay(goNotes, sizeof(goNotes) / sizeof(goNotes[0]));
    }
}

// One minute out: two short beeps, distinct from the GO pattern so you
// can tell "get ready" from "go" without looking at the screen.
static const uint16_t gunMinuteNotes[][2] = {
    { 1200, 90 },
    { 0, 70 },
    { 1200, 90 },
};

void soundGunMinute()
{
    if (!gunSoundOn) return;
    bufferedSerialPrintln("[SOUND] gun 1:00");
    soundPlay(gunMinuteNotes, sizeof(gunMinuteNotes) / sizeof(gunMinuteNotes[0]));
}

// One tick per second for the last ten. 10..4 sit at a steady pitch; 3, 2
// and 1 climb so the gun is audible in your ears, not just the screen.
void soundGunTick(int secLeft)
{
    if (!gunSoundOn || secLeft < 1 || secLeft > 10) return;

    unsigned int hz = 1000;
    if (secLeft == 3) hz = 1200;
    else if (secLeft == 2) hz = 1500;
    else if (secLeft == 1) hz = 1900;

    const uint16_t notes[1][2] = { { (uint16_t)hz, 60 } };
    bufferedSerialPrintln(String("[SOUND] gun tick ") + secLeft);
    soundPlay(notes, 1U);
}
