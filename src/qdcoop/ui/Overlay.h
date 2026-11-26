// Knights' Domain — ImGui overlay contract (i18n tab titles, ~ console dev-only). Other modules add menu tabs, toasts and world labels through this API only.
#pragma once
#include <functional>
#include <string>
#include <vector>

namespace qd::ui {

bool Init();        // hooks IDXGISwapChain::Present/ResizeBuffers + WndProc; safe to call from the game thread after core is ready
void Shutdown();

bool IsMenuOpen();  // F5 (config menuKey) toggles; while open, game keyboard/mouse input is swallowed
void SetMenuOpen(bool open);
// Dev (`kd menu <tab> [scroll 0..1]`): open the menu on that tab, filling the game window, scrolled to that fraction
// of the tab (for screenshots in the test lab).
void DebugShowMenu(const std::string& tab, float scroll);

// A tab in the F5 window. draw() runs on the render thread inside ImGui::BeginTabItem; keep it fast and never touch UObjects there —
// read state you cached on the game thread, and gamethread::Post() any action.
// `title` is an i18n key (ui/I18n.h), translated every frame so a language switch applies at once; a title that
// is not a key is shown as is.
void AddMenuTab(const char* id, const char* title, std::function<void()> draw, int order = 100);
// Tabs whose id starts with "debug" or equals "enemies" are shown in the ~ debug console window, not in the F5 menu
// (dev flavour only: the release flavour neither registers nor draws them).

#if QD_DEV
// ~ debug console (keeps the debug/cheat pages out of F5). The handler runs on the game thread
// with one command line; output comes from the log (tag "dev" plus every warning/error) and ConsolePrint().
// Dev flavour only: the release build has no ~ key handling, no console window and no log sink.
bool IsConsoleOpen();
void SetConsoleOpen(bool open);
void SetConsoleHandler(std::function<void(const std::string& line)> handler);
void ConsolePrint(const std::string& utf8);   // thread-safe
#endif

// A HUD drawer runs every frame on the render thread (same rules as a menu tab's draw), with or without the F5 menu:
// pop-up windows of a module (the trade window). It reads state cached on the game thread only.
void AddHudDrawer(const char* id, std::function<void()> draw);
// While any window holds it, the game's keyboard and mouse input is captured for the overlay (as with the F5 menu
// open). `who` names the window ("trade", "lobby"). Thread-safe.
void SetInputCapture(const char* who, bool on);

enum class ToastKind { Info, Success, Warning, Error };
void Toast(const std::string& utf8, ToastKind kind = ToastKind::Info, float seconds = 4.0f);   // thread-safe

// World-space labels (name plates). Game thread publishes the full list once per frame; render thread draws the latest list.
struct WorldLabel {
    float       screenX = 0, screenY = 0;   // pixels, already projected on the game thread
    float       alpha = 1;                  // distance fade
    std::string text;                       // UTF-8
    unsigned    color = 0xFFFFFFFF;         // ABGR (ImGui IM_COL32)
    bool        ping = false;               // 1.6.13: a ping marker at screenX/Y, text above it
};
void PublishWorldLabels(std::vector<WorldLabel> labels);   // thread-safe

}  // namespace qd::ui
