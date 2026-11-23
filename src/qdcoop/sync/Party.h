// Knights' Domain: party features: down instead of dead (a teammate revives you), party rest / party save at
// checkpoints, and trading consumables between two players. One message type (net::MsgParty), host relayed.
#pragma once
#include <cstdint>
#include <string>

namespace qd::sync {

void PartyInit();       // after coop::Init + ui::Init: message handler, hooks, ticker, the F5 "Party" tab
void PartyShutdown();
// Host: a new game in `slot` starts a new list of shared items for it.
void PartyHostNewGame(int slot);

// Game thread. The local player is down (ran out of health with a teammate who can revive them). PlayerSync shows a
// downed player as lying (the dead pose) and EnemySync stops picking them as a target.
bool PartyLocalDowned();
bool PartySlotDowned(uint8_t slot);
// Game thread. Text to add after a teammate's name plate ("down", "hold Interact to revive", "reviving 40%"); "" = none.
std::string PartyLabelSuffix(uint8_t slot);

#if QD_DEV
// Dev console "party ..." (game thread): down 0|1 (going down instead of dying, on by default), hold 0|1 (act as if
// Interact were held), trade req <slot> | accept | decline | offer <item path> <count> | confirm 0|1 | cancel,
// state (logs one "party state {...}" JSON line).
void PartyDebug(const std::string& args);
#endif

}  // namespace qd::sync
