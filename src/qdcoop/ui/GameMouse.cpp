// Knights' Domain: the game gives the mouse to the overlay while one of its windows is open (F5, the trade window).
// In the game the cursor is hidden and held in the middle of the window, so the overlay's pointer would not move. This
// does what the game's own pause menu does: show the cursor and leave it free (game and UI input); on close the game
// gets it back. When the cursor was already the game's (its own menus are open), nothing is changed. Game thread.
//
// Include order: SDK headers FIRST (see GameQueries.cpp for why).
#include "SDK/Engine_classes.hpp"
#include "SDK/UMG_classes.hpp"
#include "SDK/UMG_parameters.hpp"

#include "qdcoop/ui/GameMouse.h"

#include "qdcoop/core/Core.h"
#include "qdcoop/core/Log.h"
#include "qdcoop/core/Unreal.h"

namespace qd::ui {
namespace {

bool g_changed = false;
SDK::APlayerController* g_pc = nullptr;

template <class P>
bool CallLib(const char* fn, P& p) {
    SDK::UObject* cdo = SDK::UWidgetBlueprintLibrary::GetDefaultObj();
    SDK::UFunction* f = cdo ? ue::FindFunction(cdo, fn) : nullptr;
    if (!f) return false;
    ue::CallFunction(cdo, f, &p);
    return true;
}

}  // namespace

void GameMouseForOverlay(bool open) {
    SDK::APlayerController* pc = ue::LocalController();
    if (!pc || !ue::IsValid(pc) || !ue::LocalCharacter()) {   // the main menu already has a free cursor
        if (!open) g_changed = false;
        return;
    }
    if (open) {
        if (g_changed || pc->bShowMouseCursor) return;   // ours already, or the game's own menu has it
        pc->bShowMouseCursor = 1;
        SDK::Params::WidgetBlueprintLibrary_SetInputMode_GameAndUIEx p{};
        p.PlayerController = pc;
        p.InWidgetToFocus = nullptr;
        p.InMouseLockMode = SDK::EMouseLockMode::DoNotLock;
        p.bHideCursorDuringCapture = false;
        CallLib("SetInputMode_GameAndUIEx", p);
        g_changed = true;
        g_pc = pc;
        return;
    }
    if (!g_changed) return;
    g_changed = false;
    if (pc != g_pc) return;   // a new controller (level change): it started with the game's own settings
    pc->bShowMouseCursor = 0;
    SDK::Params::WidgetBlueprintLibrary_SetInputMode_GameOnly p{};
    p.PlayerController = pc;
    CallLib("SetInputMode_GameOnly", p);
}

}  // namespace qd::ui
