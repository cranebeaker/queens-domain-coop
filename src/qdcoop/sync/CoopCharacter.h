// Knights' Domain: the host keeps every friend's character. A friend plays a co-op character in a save slot the main
// menu never shows (kCoopSlot). The host stores that save, per friend (Steam id) and per host save slot, under its own
// KnightsDomain folder; when the friend joins (or the host starts from the waiting room) the host sends it, the friend's
// game loads it, and every time the friend's game writes it the host gets the new copy. No stored character yet: the
// friend starts a fresh one. The friend's own saves (slots 0 to 4) are never touched. One message type
// (net::MsgCharacter). Game thread.
#pragma once
#include <cstdint>

namespace qd::sync {

constexpr int kCoopSlot = 8;   // the friend's co-op character (the menu shows 0 to 4)

void CharacterInit();   // after coop::Init: message handler, ticker

// Client, on the main menu: ask the host for this player's character and boot into it. togetherFresh: the host started a
// new game from the waiting room at the same moment (no teleport to the host afterwards).
void CharacterBoot(bool togetherFresh);
bool CharacterBooting();
// 1.6.11: the host's game was saved: this friend's character is saved too (as soon as it safely can), so a game over
// takes everyone back to the same moment
void CharacterHostSaved();   // asked or loading: the waiting room leaves the menu alone

// Host: it starts a new game in `slot`, so the friends' characters kept for that slot belong to the old game.
void CharacterHostNewGame(int slot);

}  // namespace qd::sync
