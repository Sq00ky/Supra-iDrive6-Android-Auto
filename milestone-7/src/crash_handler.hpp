// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef CRASH_HANDLER_HPP
#define CRASH_HANDLER_HPP

namespace nbt {

// Last-known-position breadcrumbs, written with plain stores from any thread and
// printed by the fatal-signal handler (so a crash log says where we were).
extern volatile int g_crumb_stage;     // coarse stage id (see aa_session.cpp)
extern volatile int g_crumb_channel;   // last AA channel number received
extern volatile int g_crumb_msg;       // last AA message id received

// Line-buffer stdout (logs survive a crash), ignore SIGPIPE (a send() to a phone that
// just closed would otherwise kill the process silently) and install fatal-signal
// reporting. Call first thing in main().
void installProcessGuards();

// Optional: called from the fatal-signal handler before the report, so a feature can undo an outside effect (kombi_nav
// clears a cluster/HUD arrow). Must be async-signal-safe.
extern void (*volatile g_fatal_hook)();

// Optional, set by the log pump (log_pump.hpp): after the report and the fatal hook, a bounded wait for the pump to
// write the report out (async-signal-safe). And a second file the report is also written to (a RAM path, so the line
// survives even when the log file's USB stick has hung).
extern void (*volatile g_log_drain_hook)();
extern const char* volatile g_crash_copy_path;

} // namespace nbt

#endif
