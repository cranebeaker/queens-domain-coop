// Knights' Domain: the waiting room. While a lobby is on the main menu, the "Play Game" buttons are held until every
// member has readied up; then the host picks a save (or a new game) on the main menu and every machine boots into its
// own save of that slot at the same moment. One message type (net::MsgLobby). Game thread unless noted.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace qd::sync {

void LobbyInit();       // after coop::Init + ui::Init: message handler, hooks, ticker
void LobbyShutdown();

struct LobbyRow {
    uint8_t     slot = 0;
    std::string name;
    bool        ready = false;
    bool        isLocal = false;
    bool        isHost = false;
};
struct LobbyView {
    bool                  waiting = false;     // in a session, on the main menu, the host has not started yet
    bool                  isHost = false;
    bool                  allReady = false;    // every client is ready (the host picks the save now)
    bool                  localReady = false;
    bool                  inSession = false;
    bool                  onTitle = false;
    bool                  started = false;
    std::vector<LobbyRow> rows;
};

// Thread-safe (the F5 window reads it on the render thread); refreshed about four times a second.
LobbyView LobbyViewNow();
// Thread-safe: the local player is ready / not ready (clients only; the host is always ready).
void LobbySetReady(bool ready);
// Thread-safe: the host presses the main menu's own Play Game (its save menu opens) once everyone is ready.
void LobbyHostPlayNow();

#if QD_DEV
// Dev console "lobby": logs "lobby state {...}" (one JSON line); "lobby ready 0|1"; "lobby start <slot> <new 0|1>"
// runs the start on this machine as a client does when the host picks a save.
void LobbyDebug(const std::string& args);
#endif

}  // namespace qd::sync
