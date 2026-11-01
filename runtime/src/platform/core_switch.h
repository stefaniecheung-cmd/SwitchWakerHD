// SwitchWakerHD (this fork): the Switch parts of core.cpp (core_switch.cpp): the guest memory window.
// The buffered SD-card log (log_flush, heap_never_used_mib, log_heap: runtime.h) and the pthread/thrd wraps
// live there too and need no call.
#pragma once

namespace core_switch {
void init_memory();  // mem::init(): back the guest ranges in a reserved 4 GiB code-memory window
// (this fork v6) the orderly exit: give those mappings and their heap pages back before the loader
// takes the process over (release_memory is only safe once every other thread is frozen)
void release_memory();
// (this fork v7) ask appletOE to close this applet normally, then park until the system closes us.
// The loader-relay quit (std::_Exit) crashes the loader on our leftover code-memory mappings, and
// svcExitProcess gets the "software was closed" dialog; this is the loader's own selfExit, done here.
void request_applet_exit();
}  // namespace core_switch

// (this fork) opens logs/crash_latest.txt for the crash handler's own descriptor (main_switch.cpp calls
// it right after the session log's freopen; log_crash_write below writes every crash text to both files)
void crash_log_open();
void log_crash_write(const char* text, size_t n);  // also used directly by the exception handler
