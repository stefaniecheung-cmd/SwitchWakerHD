// SwitchWakerHD (this fork): host thread placement and priorities, included from host.h. They act on the
// Switch (Horizon cores and priorities) and do nothing elsewhere, so shared code calls them without #ifdefs.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>
#ifdef __SWITCH__
#include <pthread.h>
#include <switch.h>
#include <atomic>
#endif

namespace host {
// Horizon starts every thread on the process's default core; let the scheduler use the three
// application cores, preferring `core` (the emulated Espresso core, or any host thread's choice).
inline void set_thread_core(uint32_t core) {
#ifdef __SWITCH__
 svcSetThreadCoreMask(threadGetCurHandle(),(s32)(core%3),0x7);
#else
 (void)core;
#endif
}
// WWHD_CORE_LAYOUT (on by default on the Switch, =0 for the old placement): the game's main thread
// (emulated core 1) gets host core 1 to itself; the render thread, Mesa's GL thread and the guest
// threads of emulated cores 0 and 2 share host cores 0 and 2. Before, the higher-priority host
// threads kept landing on the main thread's core and preempting it (it held its emulated core 70%
// of the time but ran only 63%), and the main thread is what limits the frame rate in busy views.
inline bool core_layout_on() {
#ifdef __SWITCH__
    static const bool on = [] {
        const char* e = getenv("WWHD_CORE_LAYOUT");
        return !(e && *e == '0');
    }();
    return on;
#else
    return false;
#endif
}
// a thread's preferred host core and the set it may run on (bit n: core n); Switch only
inline void set_thread_cores(uint32_t preferred, uint32_t mask) {
#ifdef __SWITCH__
 svcSetThreadCoreMask(threadGetCurHandle(), (s32)preferred, mask);
#else
 (void)preferred; (void)mask;
#endif
}
// placement of a thread of emulated core `core` (guest threads, the render and GL threads)
inline void place_thread(uint32_t core) {
    if (!core_layout_on()) return set_thread_core(core);
    if (core % 3 == 1) set_thread_cores(1, 0x2);
    else set_thread_cores(core % 3, 0x5);
}
// libnx creates every pthread at priority 59, the only one Horizon time-slices (10 ms) on cores 0-2.
// Host service threads (GX2 render, audio, alarms) move above the guest threads so they run as soon
// as they have work instead of waiting for a busy guest thread's slice to end.
inline void raise_thread_priority() {
#ifdef __SWITCH__
 svcSetThreadPriority(threadGetCurHandle(),0x2C);
 // these run above the guest threads: off the main thread's core (WWHD_CORE_LAYOUT)
 if (core_layout_on()) set_thread_cores(0, 0x5);
#endif
}
// (this fork) the orderly shutdown's thread registry. The menu quit ends the host loop and returns to
// Sphaira/hbmenu the way homebrew is designed to: std::_Exit runs __appExit (fsdev, fs, time, hid,
// applet, sm are closed one by one) and then libnx jumps into the loader's exit function. The loader
// (hbl, mapped at a fixed address in this same process — read from Sphaira's hbl/source/main.c) then
// unmaps our NRO and, since we set no next-load, reloads the default NRO (Sphaira) — the process stays
// alive, no "software was closed" error (svcExitProcess kills that whole process, and a self-dead
// applet IS that error even with result 0 and no crash report — the 07:25/07:46 sessions).
// hbl does NOT clean up after us: it unmaps our code while anything still executing it dies (report
// 01793494051, the 08:44 session: the crash came AFTER the unmap — the module list already held only
// hbl while three of our threads still ran our unmapped code). v3 paused the threads but swept ONCE:
// a thread registering during the sweep was missed, and one created during the shutdown was never
// registered at all (its frozen body sat in start_thread). So: the flag goes up first (new creations
// are refused and every thread's entry self-exits on it), and the sweep runs in rounds — any thread
// alive in round n is frozen by round n+1, and a thread can only create threads while it is itself
// still running, so after the last round every non-main thread of ours is frozen (svcSetThreadActivity,
// the kernel pause the thread sampler uses every second). Horizon has no TerminateThread for userland,
// but frozen threads execute nothing: the unmap is safe, and the frozen bodies stay out of Sphaira's
// way (they hold only their stacks — the same kind of remains any homebrew leaves in hbl's heap).
// Threads register their own handle at their start (threadGetCurHandle returns a real full-access
// handle fetched via svcGetHandle, which is what lets one thread act on another); the sweep ignores
// errors (a handle of a thread that already exited is simply stale).
#ifdef __SWITCH__
inline Handle g_shutdownThreads[128];
inline u64 g_shutdownTids[128];  // (v4b) who each slot was: a handle is recycled once its thread exits
inline std::atomic<int> g_shutdownThreadCount{0};
inline std::atomic<bool> g_shutdownStarted{false};
// what the sweep found (logged by the caller before the exit): the crash report of a failed exit lists the
// thread ids that survived, so a run that still crashes says whether they were ours, and why pausing failed
inline char g_shutdownReport[512] = {0};
// (v6) every thread that was still alive, not just the few that fit in the report: tid and where it was
// (the pc minus the NRO's base is the offset to look up in wwhd.elf)
inline int g_aliveThreadCount = 0;
inline unsigned long long g_aliveTids[32] = {0};
inline unsigned long long g_alivePcs[32] = {0};
#endif
inline void pause_register() {
#ifdef __SWITCH__
    if (g_shutdownStarted.load(std::memory_order_acquire)) svcExitThread();  // no new threads past the sweep
    const int i = g_shutdownThreadCount.fetch_add(1, std::memory_order_relaxed);
    if (i >= 0 && i < 128) {
        g_shutdownThreads[i] = threadGetCurHandle();
        // (v4b) the number, not just the handle: Horizon recycles handle values the moment a thread exits, so a
        // stale slot can hold an event, a session, or a later thread. The sweep only pauses a slot whose handle
        // still reports this id (v4 paused whatever the recycled handle belonged to, and nothing else).
        u64 tid = 0;
        g_shutdownTids[i] = R_SUCCEEDED(svcGetThreadId(&tid, g_shutdownThreads[i])) ? tid : 0;
    }
    // the shutdown may have started while this thread sat between the check and the registration:
    // re-check and die rather than run on into the teardown (a created-but-unregistered thread would
    // otherwise execute our code after the loader unmaps it — report 01793494051's start_thread freeze)
    if (g_shutdownStarted.load(std::memory_order_acquire)) svcExitThread();
#endif
}
inline void shutdown_threads() {
#ifdef __SWITCH__
    g_shutdownStarted.store(true, std::memory_order_release);
    const Handle self = threadGetCurHandle();
    const int n = g_shutdownThreadCount.load(std::memory_order_relaxed);
    int alive = 0, paused = 0, gone = 0, stale = 0, failed = 0;
    unsigned badResult = 0;
    // (v6) a slot is frozen once. The sweep runs in rounds, and svcSetThreadActivity on a thread that
    // is already paused answers InvalidState (0xFA01): v5 read that as "nothing could be paused" and
    // reported 69 alive and 0 paused when the truth was the opposite — every one of them frozen in
    // round 0 by a pause the later rounds then only re-tried (reports 01793503748 / 01793503962).
    bool frozen[128] = {false};
    unsigned long long* aliveTids = g_aliveTids;
    unsigned long long* alivePcs = g_alivePcs;  // (v5) where each one was: look the addresses up in wwhd.elf
    int aliveTidCount = 0;
    // rounds: the set of running threads can only shrink from here (new creations are refused above),
    // and anything alive when its entry is reached is frozen; the sleeps between rounds also give a
    // creation still in flight (its body not yet entered) time to surface and self-exit.
    // Each pause is verified: svcGetThreadContext3 only dumps a thread that svcSetThreadActivity paused,
    // so `paused` counts the threads that provably execute nothing any more (a waiting thread that only
    // had its activity flag set cannot run when the wait completes: the kernel resumes it to paused).
    for (int round = 0; round < 8; round++) {
        alive = paused = gone = stale = failed = 0;
        aliveTidCount = 0;
        for (int i = 0; i < n && i < 128; i++) {
            const Handle h = g_shutdownThreads[i];
            if (!h || h == self) continue;
            u64 tid = 0;
            // (v4b) the handle must still be this thread: an exited thread's handle value is recycled at once,
            // and svcSetThreadActivity on the new owner would pause something unrelated (or error, silently)
            if (R_FAILED(svcGetThreadId(&tid, h)) || tid == 0 || tid != g_shutdownTids[i]) {
                stale++;
                continue;
            }
            if (R_SUCCEEDED(waitSingleHandle(h, 0))) {  // signaled: the thread has exited
                gone++;
                continue;
            }
            alive++;
            const int k = aliveTidCount < 32 ? aliveTidCount++ : -1;
            if (k >= 0) aliveTids[k] = tid;
            if (frozen[i]) {  // paused in an earlier round and still not running: leave it alone
                paused++;
                continue;
            }
            const Result rc = svcSetThreadActivity(h, ThreadActivity_Paused);
            if (R_FAILED(rc)) {
                failed++;
                if (!badResult) badResult = (unsigned)rc;
                continue;
            }
            ThreadContext ctx;
            if (R_SUCCEEDED(svcGetThreadContext3(&ctx, h))) {
                frozen[i] = true;
                paused++;
                if (k >= 0) alivePcs[k] = ctx.pc.x;
            } else {
                failed++;
                if (!badResult) badResult = 0xFFFFFFFFu;  // paused but the context dump refused
                frozen[i] = true;  // the pause itself was accepted: it cannot run any more
                paused++;
            }
        }
        if (alive == 0 || paused == alive) break;
        svcSleepThread(40ULL * 1000 * 1000);  // 40 ms
    }
    int off = snprintf(g_shutdownReport, sizeof g_shutdownReport,
                       "[exit] threads: %d registered, %d exited, %d stale, %d alive, %d paused, %d not frozen%s%08x",
                       n, gone, stale, alive, paused, failed,
                       badResult ? ", first pause error " : " (every pause accepted)", badResult);
    // (v6) the whole list, one line each: the 512-byte report only ever held the first few, and the
    // addresses are what say which threads they were (aarch64-none-elf-addr2line against wwhd.elf).
    // Logged by the caller (this header has no LOG): g_aliveThreadCount / g_aliveTids / g_alivePcs.
    g_aliveThreadCount = aliveTidCount;
    if (off > 0 && (size_t)off < sizeof g_shutdownReport) {
        off += snprintf(g_shutdownReport + off, sizeof g_shutdownReport - (size_t)off, "; alive:");
        for (int i = 0; i < aliveTidCount && off > 0 && (size_t)off < sizeof g_shutdownReport; i++)
            off += snprintf(g_shutdownReport + off, sizeof g_shutdownReport - (size_t)off, " %llx@%llx", aliveTids[i],
                            alivePcs[i]);
    }
    svcSleepThread(60ULL * 1000 * 1000);  // a last breath for anything still surfacing
#endif
}
// A detached host thread. Horizon commits a small default stack for std::thread, so threads that run
// guest code or the shader compilers ask for `stack` bytes there.
inline void start_thread(void (*fn)(), size_t stack) {
#ifdef __SWITCH__
 if (g_shutdownStarted.load(std::memory_order_acquire)) return;  // no new threads past the sweep
 pthread_attr_t attr; pthread_attr_init(&attr); pthread_attr_setstacksize(&attr,stack);
 pthread_t t;
 if(pthread_create(&t,&attr,[](void* f)->void* { raise_thread_priority(); pause_register(); ((void(*)())f)(); return nullptr; },(void*)fn)==0) pthread_detach(t);
 pthread_attr_destroy(&attr);
#else
 (void)stack; std::thread(fn).detach();
#endif
}

}  // namespace host
