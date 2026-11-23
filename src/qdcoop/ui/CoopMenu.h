// QD Coop — the F5 window's "联机 / 我的名字 / 调试" tabs.
#pragma once

namespace qd::ui {

// Game thread, after coop::Init(). Registers the tabs and a ticker that snapshots co-op state for the render thread.
void InitCoopMenu();
// The co-op lobby window (the main menu's Co-op button, F5 > Fix > Co-op lobby). Thread-safe.
void ShowLobby(bool open);
bool LobbyShown();
void ShutdownCoopMenu();

}  // namespace qd::ui
