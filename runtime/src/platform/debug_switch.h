// The game's side of the debug server (debug_server.h): the Switch tab's "Debug server (network)" setting
// (settings.ini switchDebugServer=1; off by default) starts it at boot on port 6543, with the commands info,
// warps, warp, shot, reload and quit (debug_switch.cpp).
#pragma once

namespace debug_switch {
void start();           // main(), first thing once the log is open
bool quit_requested();  // the host loop ends ("quit")
void request_quit();    // (Settings > Quit Game)
void request_quit_soft();  // (this fork) ends the host loop without debugsrv::stop: the menu quit's
                           // sockets may stay open, the orderly shutdown does not need them closed
bool running();         // the server was started this session
int port();
}  // namespace debug_switch
