// SwitchWakerHD (this fork): the Switch parts of core.cpp, behind its marked hooks (core_switch.h): the
// buffered SD-card log and its writer thread, heap statistics, libnx thread fixes and the guest memory window.
#include "core_switch.h"

#include <switch.h>
#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

#include "../runtime.h"
#include "../mods/guest_builtin.h"  // (this fork) guest mods built into the NRO: their guest memory
#include "debug_server.h"
#include "host.h"

// The log is a file on the SD card: a write can take milliseconds, and the thread that logs (render,
// audio, the game's main thread) would stall for it. core.cpp's log_msg hands every line to the sink
// below (under its log lock), which keeps it in a buffer that a writer thread writes out four times a
// second. fatal() and log_flush() write it at once.
static std::mutex g_pending_mutex;
static std::string g_log_pending;
static constexpr size_t kLogPendingMax = 4 << 20;  // dropped beyond this (a flood must not eat memory; a capture
                                                   // frame's per-draw trace is ~1.5 MB)

void log_flush() {
    std::string out;
    {
        std::lock_guard<std::mutex> lk(g_pending_mutex);
        out.swap(g_log_pending);
    }
    if (!out.empty()) {
        fwrite(out.data(), 1, out.size(), stderr);  // (logs/wwhd_<date>_<time>.log, main_switch.cpp)
        fflush(stderr);
        debugsrv::log_tap(out.data(), out.size());  // "log" streams of the debug server (when it runs)
    }
}

static void log_writer_start() {
    static std::once_flag once;
    std::call_once(once, [] {
        pthread_t t;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, 64 << 10);
        if (pthread_create(&t, &attr, [](void*) -> void* {
                host::pause_register();  // (this fork v3) the orderly shutdown pauses this thread
                // above the game's threads (59), like the other host service threads: the log keeps
                // reaching the SD card while game threads are busy or stuck
                host::raise_thread_priority();  // and off the main thread's core
                for (;;) {
                    svcSleepThread(250'000'000ll);
                    log_flush();
                }
                return nullptr;
            }, nullptr) == 0)
            pthread_detach(t);
        pthread_attr_destroy(&attr);
    });
}

static void log_sink(const char* line, size_t n) {
    {
        std::lock_guard<std::mutex> lk(g_pending_mutex);
        if (g_log_pending.size() + n + 1 <= kLogPendingMax) {
            g_log_pending.append(line, n);
            g_log_pending += '\n';
        }
    }
    log_writer_start();
}
// before the static initialisers that log (101 = the first priority for programs, like settings.ini's
// reader in main_switch.cpp, which logs nothing itself); without the sink a line would reach no file
__attribute__((constructor(101))) static void log_sink_install() { log_set_sink(log_sink); }

// From the CPU exception handler (main_switch.cpp): the pending lines and then text, written straight to the
// files' descriptors. Nothing waits for a lock: the thread that crashed may hold the log's or stdio's.
// (this fork) The text also goes to logs/crash_latest.txt through its own descriptor: if the session log's
// file state is what the exception left broken (it can be, when the fault is inside the fsdev/SD write path),
// the second file is another chance, and both are fsynced so neither waits for the writer thread's flush.
static int g_crash_fd = -1;
void crash_log_open() { g_crash_fd = open("logs/crash_latest.txt", O_WRONLY | O_CREAT | O_TRUNC, 0666); }

void log_crash_write(const char* text, size_t n) {
    std::string out;
    if (g_pending_mutex.try_lock()) {
        out.swap(g_log_pending);
        g_pending_mutex.unlock();
    }
    for (const int fd : {fileno(stderr), g_crash_fd}) {
        if (fd < 0) continue;
        if (!out.empty()) (void)!write(fd, out.data(), out.size());
        (void)!write(fd, text, n);
        fsync(fd);
    }
}

// the heap malloc has never grown into, in MiB (libnx's heap end against malloc's break; no lock).
// Freed blocks inside the used part come on top of it.
extern "C" char* fake_heap_end;
size_t heap_never_used_mib() {
    char* top = static_cast<char*>(sbrk(0));
    if (!fake_heap_end || !top || top == reinterpret_cast<char*>(-1)) return 0;
    return size_t(fake_heap_end - top) >> 20;
}
void log_heap(const char* when) { LOG("[mem] %s: %zu MiB of heap never used", when, heap_never_used_mib()); }

// ---------------------------------------------------------------- threads
extern "C" { uint8_t* ppc_mem_base_var = nullptr; }
// libnx implements no pthread_detach hook (it returns ENOSYS, which makes std::thread::detach throw).
// Detached host threads here run for the whole session, so not reclaiming them on exit is fine.
struct __pthread_t;
extern "C" int __syscall_thread_detach(struct __pthread_t*) { return 0; }
// libnx gives a thread created without attributes a 128 KiB stack. Mesa creates its GL thread
// (WWHD_GL_THREAD) that way, and that thread runs the GLSL compiler, which needs far more.
// The link wraps pthread_create (CMakeLists.txt) so such threads get 4 MiB.
extern "C" int __real_pthread_create(pthread_t*, const pthread_attr_t*, void* (*)(void*), void*);
// (this fork v5) Every host thread registers here, whoever creates it: pausing only the sites that call
// pause_register themselves still missed some (report 01793501613 — two threads of the previous run woke up
// minutes later in code the loader had already unmapped). One shim covers libstdc++'s std::thread, libnx's
// pthreads and anything a library starts, present or future; a thread that also calls pause_register in its
// body just registers twice.
namespace {
struct ThreadShim {
    void* (*fn)(void*);
    void* arg;
};
void* thread_shim(void* p) {
    host::pause_register();
    ThreadShim s = *static_cast<ThreadShim*>(p);
    delete static_cast<ThreadShim*>(p);
    return s.fn(s.arg);
}
}  // namespace
extern "C" int __wrap_pthread_create(pthread_t* thread, const pthread_attr_t* attr, void* (*fn)(void*), void* arg) {
    size_t size = 0;
    auto* s = new ThreadShim{fn, arg};  // freed by the thread it starts
    int r;
    if (attr && pthread_attr_getstacksize(attr, &size) == 0 && size) {
        r = __real_pthread_create(thread, attr, thread_shim, s);
    } else {
        pthread_attr_t big;
        if (attr) big = *attr;
        else pthread_attr_init(&big);
        pthread_attr_setstacksize(&big, 4 << 20);
        r = __real_pthread_create(thread, &big, thread_shim, s);
        if (!attr) pthread_attr_destroy(&big);
        if (r) LOG("[boot] pthread_create with a 4 MiB stack failed (%d)", r);
    }
    if (r) delete s;
    return r;
}
// newlib's C11 threads define thrd_success as 4; Mesa (switch-mesa, built against newlib's <threads.h>
// but with its own success test, `if (ret) fail`) treats that as a failure. Its util_queue then frees
// the queue of a thread that is already running, and Mesa's GL thread (WWHD_GL_THREAD) never starts.
// Mesa is the only caller of thrd_create, and it only tests for zero: success becomes 0 here.
extern "C" int __real_thrd_create(void* thread, int (*fn)(void*), void* arg);
extern "C" int __wrap_thrd_create(void* thread, int (*fn)(void*), void* arg) {
    int r = __real_thrd_create(thread, fn, arg);
    if (r != 4 /* newlib thrd_success */) LOG("[boot] thrd_create failed (%d)", r);
    return r == 4 ? 0 : (r ? r : 2);
}

// ---------------------------------------------------------------- guest memory
// Horizon commits every mapping, so only the guest ranges the game and the runtime use are backed:
// heap pages are aliased into a reserved 4 GiB window (code-memory region, read/write). Everything the
// desktop builds can touch (all of it is mapped there) except the large gaps is backed.
namespace {
struct GuestRange { uint32_t start, size; };
constexpr GuestRange kGuestRanges[] = {
    {0x00010000, 0x04000000 - 0x00010000},  // low memory and .text / .rodata of cking.rpx
    {mem::kMem2Start, mem::kMem2End - mem::kMem2Start},
    {mem::kRuntimeStart, mem::kRuntimeEnd - mem::kRuntimeStart},
    {0xC0000000, 0x02100000},          // import slots, data imports, host function addresses
    {mem::kFgBucket, mem::kFgBucketSize},
    {mem::kMem1, mem::kMem1Size},
};
// (this fork v6) what init_memory mapped, so the exit can give it back. When we return, the loader
// (hbl) unmaps our three segments and then maps the next NRO wherever its own bookkeeping of the
// code-memory region says there is room — and that bookkeeping has never heard of this window. A
// mapping of ours left behind is a hole it cannot see: reports 01793503748 / 01793503962 aborted in
// hbl itself (result 0xD401, InvalidMemoryState, the error its own code names for a code-memory
// syscall that finds the wrong state). Everything mapped here goes back before we hand over.
struct GuestMap { void* dst; void* backing; uint32_t size; };
GuestMap g_guestMaps[std::size(kGuestRanges) + 1];
void* g_guestWindow = nullptr;
VirtmemReservation* g_guestReservation = nullptr;  // (v7) the 4 GiB address-space reservation, released on the way out
}  // namespace
void core_switch::init_memory() {
    if (!envIsSyscallHinted(0x77) || !envIsSyscallHinted(0x73))
        fatal("guest memory needs svcMapProcessCodeMemory/svcSetProcessMemoryPermission (run from hbmenu over a game, "
              "not the album applet)");
    Handle self = envGetOwnProcessHandle();
    virtmemLock();
    void* window = virtmemFindCodeMemory(0x100000000ull, 0x10000);
    g_guestReservation = window ? virtmemAddReservation(window, 0x100000000ull) : nullptr;
    if (!window || !g_guestReservation) {
        virtmemUnlock();
        fatal("cannot reserve a 4 GiB guest address window");
    }
    virtmemUnlock();
    uint64_t total = 0;
    // the guest memory of the guest mods built into the NRO (mods/guest_builtin.h), when it has any
    GuestRange ranges[std::size(kGuestRanges) + 1];
    size_t n = 0;
    for (const auto& r : kGuestRanges) ranges[n++] = r;
    if (const guestbuiltin::Region mods = guestbuiltin::memory(); mods.size) ranges[n++] = {mods.start, mods.size};
    for (size_t i = 0; i < n; i++) {
        const GuestRange& r = ranges[i];
        void* backing = aligned_alloc(0x1000, r.size);
        if (!backing)
            fatal("out of memory backing guest range %08X (+%X); start the game with full RAM (hold R on a title)",
                  r.start, r.size);
        memset(backing, 0, r.size);
        uint8_t* dst = (uint8_t*)window + r.start;
        Result rc = svcMapProcessCodeMemory(self, (u64)dst, (u64)backing, r.size);
        if (R_FAILED(rc)) fatal("svcMapProcessCodeMemory %08X failed (0x%X)", r.start, rc);
        rc = svcSetProcessMemoryPermission(self, (u64)dst, r.size, Perm_Rw);
        if (R_FAILED(rc)) fatal("svcSetProcessMemoryPermission %08X failed (0x%X)", r.start, rc);
        if (i < std::size(g_guestMaps)) g_guestMaps[i] = {dst, backing, r.size};
        total += r.size;
    }
    g_guestWindow = window;
    ppc_mem_base_var = (uint8_t*)window;
    LOG("[mem] guest window at %p, %llu MiB backed", window, (unsigned long long)(total >> 20));
    log_heap("after the guest memory");
}
// (this fork v6) called when the host loop has ended and every other thread is frozen: unmap the
// guest ranges (svcUnmapProcessCodeMemory needs the same destination/source pair the map used) and
// free their heap pages, so the code-memory region the loader searches is empty of us again. Nothing
// runs from these pages and no frozen thread holds them: guest memory is data, our code is in the
// NRO's own segments, which the loader unmaps itself.
void core_switch::release_memory() {
    if (!g_guestWindow) return;
    const Handle self = envGetOwnProcessHandle();
    for (const GuestMap& m : g_guestMaps) {
        if (!m.dst || !m.backing) continue;
        const Result rc = svcUnmapProcessCodeMemory(self, (u64)m.dst, (u64)m.backing, m.size);
        LOG("[exit] guest %p (%llu MiB): unmap %s (0x%X)", m.dst, (unsigned long long)(m.size >> 20),
            R_SUCCEEDED(rc) ? "ok" : "FAILED", rc);
    }
    for (GuestMap& m : g_guestMaps) {
        if (m.backing) free(m.backing);
        m = {nullptr, nullptr, 0};
    }
    // (v7) release the 4 GiB address-space reservation too, not just the backed mappings. The loader's
    // own virtmem bookkeeping is separate from ours, but the code-memory region is shared: leaving the
    // reservation in place left the kernel's view of the region inconsistent, and the loader's next
    // svcMapProcessCodeMemory answered InvalidMemoryState (0xD401). Give every piece back.
    if (g_guestReservation) {
        virtmemLock();
        virtmemRemoveReservation(g_guestReservation);
        virtmemUnlock();
        g_guestReservation = nullptr;
    }
    LOG("[exit] guest window %p given back (4 GiB of the code-memory region)", g_guestWindow);
    g_guestWindow = nullptr;
    log_heap("after the guest memory was given back");
}
// (this fork v7) the quit no longer hands back to the loader (std::_Exit → hbl unmaps us → hbl maps
// the next NRO): that path re-runs svcMapProcessCodeMemory/svcSetProcessMemoryPermission over a
// code-memory region the loader's own bookkeeping never reconciled with our 4 GiB guest window, and
// the loader aborts with InvalidMemoryState (0xD401) — the 11:27/11:29/12:50 exits. The loader itself
// (Sphaira's hbl, main.c selfExit) quits its applet by asking appletOE to exit and then sleeping until
// the system closes it: no code-memory work, no "software was closed" dialog (which svcExitProcess, a
// self-dead applet, gets). Do the same here: request the applet exit, then park forever; the system
// closes the process from the outside. Reaching here means the host loop ended and every other thread
// is frozen, so nothing else runs into the teardown.
void core_switch::request_applet_exit() {
    // The sm session may already be closed by the time we quit (the hbloader that launched this NRO
    // tore down its own sm, and ours can have gone stale). hbl's selfExit() calls smInitialize() first
    // for exactly this reason; smInitialize() is idempotent, so mirror it here.
    Service applet, proxy, self;
    Result rc = smInitialize();
    if (R_FAILED(rc)) { LOG("[exit] applet exit: smInitialize failed 0x%X", rc); log_flush(); svcExitProcess(); }
    rc = smGetService(&applet, "appletOE");
    if (R_FAILED(rc)) { LOG("[exit] applet exit: smGetService(appletOE) failed 0x%X", rc); log_flush(); smExit(); svcExitProcess(); }
    // GetSessionProxy (cmd 0): pass our process handle so the applet controller can act on this applet.
    const u64 reserved = 0;
    rc = serviceDispatchIn(&applet, 0, reserved,
        .in_send_pid = true,
        .in_num_handles = 1,
        .in_handles = { envGetOwnProcessHandle() },
        .out_num_objects = 1,
        .out_objects = &proxy);
    if (R_FAILED(rc)) { LOG("[exit] applet exit: GetSessionProxy failed 0x%X", rc); log_flush(); serviceClose(&applet); svcExitProcess(); }
    // GetSelfController (cmd 1).
    rc = serviceDispatch(&proxy, 1, .out_num_objects = 1, .out_objects = &self);
    if (R_FAILED(rc)) { LOG("[exit] applet exit: GetSelfController failed 0x%X", rc); log_flush(); serviceClose(&proxy); serviceClose(&applet); svcExitProcess(); }
    // Exit (cmd 0): the applet closes itself normally (no error dialog).
    rc = serviceDispatch(&self, 0);
    LOG("[exit] applet exit requested: 0x%X", rc);
    log_flush();  // the log writer thread is frozen by now (shutdown_threads): flush before we park forever
    serviceClose(&self);
    serviceClose(&proxy);
    serviceClose(&applet);
    if (R_SUCCEEDED(rc)) {
        // The system is closing us: park until it does. Never svcExitProcess (that is the self-dead
        // "software was closed" case); if the close never comes the kernel's own timeout ends us.
        while (true) svcSleepThread(86400000000000ULL);
    }
    svcExitProcess();  // last resort: the request failed, prefer an error dialog over a hung applet
}
