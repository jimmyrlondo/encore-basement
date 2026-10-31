// Encore: Basement Studio
// Copyright (C) 2026 Encore Studio. AGPLv3 (see LICENSE).
//
// The few Windows calls the 32-bit plugin bridge needs (shared memory and
// events), behind plain functions so <windows.h> stays in Shm.cpp only.
#pragma once

#include <cstddef>

namespace bridgeos
{
    struct Memory;
    struct Event;

    /** Named shared memory: create makes it (zeroed), open maps one another process made */
    Memory* createMemory (const wchar_t* name, size_t bytes);
    Memory* openMemory (const wchar_t* name, size_t bytes);
    void* address (Memory*);
    void close (Memory*);

    /** Named auto-reset events */
    Event* createEvent (const wchar_t* name);
    Event* openEvent (const wchar_t* name);
    void signal (Event*);
    bool wait (Event*, int milliseconds);   // true if signalled in time
    void close (Event*);

    /** The machine a .dll / .vst3 binary is built for: 32 (x86), 64 (x64 / ARM64), or 0 if it isn't one */
    int binaryBits (const wchar_t* path);

    /** Whether a process has a window showing (an activation prompt, say), and ending one */
    bool processHasWindow (unsigned long pid);
    void killProcess (unsigned long pid);
    unsigned long currentProcessId();

    /** For a process that's only checking a plugin: any window it shows (an activation prompt, a
        splash) is hidden straight away, so nothing flashes up on screen. If the plugin still hasn't
        finished giveUpMs after showing one, it's waiting on that window: the flag file (if any) is
        made and the process ends with exit code 3, so the plugin is set aside. */
    void hideWindowsWhileScanning (int giveUpMs, const wchar_t* flagFile);
}
