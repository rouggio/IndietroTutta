#pragma once

// "Why did the board start?" — recorded once, at boot, before anything else
// can overwrite it.
//
// The instrument reboots in the field (a race finish is the observed case) and
// leaves nothing behind to explain it: the log ring is in heap and the panic
// backtrace goes to the UART only. So the first thing setup() does is ask the
// SoC. esp_reset_reason() distinguishes a software restart (our own
// ESP.restart) from a panic (abort / stack canary / exception), a task or
// interrupt watchdog, and a brownout — which is the difference between a code
// bug and a power problem, i.e. the only question worth asking first.
void bootDiagInit();

// This boot's reason, lower-case and stable for the API: "power-on",
// "software", "panic", "task-wdt", "int-wdt", "brownout", ...
const char* bootResetName();

// Persisted in NVS ("diag" namespace), so they survive the reboot itself.
int bootCount();        // total boots ever recorded on this board
int bootPanicCount();   // boots that followed a panic/watchdog reset
