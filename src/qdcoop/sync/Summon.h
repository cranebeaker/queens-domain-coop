// QD Coop — "teleport to the host": F5 -> 传送 tab for clients, plus the "back at your save
// point" toast after a client dies and reloads. Init/Shutdown/tick on the game thread.
#pragma once

namespace qd::sync {

void SummonInit();       // menu tab + ticker; call after ui::Init
void SummonShutdown();
// Client: teleport next to the host now (as the Teleport tab does). Game thread.
void SummonToHost();
// Teleport next to the player in `slot` (any player, the host included). Game thread.
void SummonTo(unsigned char slot);

}  // namespace qd::sync
