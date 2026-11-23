// Knights' Domain: the co-op page of the main menu. The main menu's own buttons (Play Game, Co-op, Options, Credits,
// Quit) are swapped for co-op items built from the game's own widgets (FS_Button, its text style, its face textures),
// in the same column; Back puts the menu back. Game thread.
#pragma once

namespace SDK { class UObject; }

namespace qd::ui {

void CoopPageInit();                       // after TitleButtonInit: click hook + ticker
void CoopPageOpen(SDK::UObject* title);    // the main menu's Co-op button was pressed
bool CoopPageIsOpen();
#if QD_DEV
void CoopPageDebugOpen();   // dev console "page": open the page on the main menu (lab screenshots)
#endif

}  // namespace qd::ui
