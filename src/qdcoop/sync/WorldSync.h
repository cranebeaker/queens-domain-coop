// Knights' Domain: shared world state: saved world objects (chests, doors, levers, lifts, secret walls, barricades,
// pickups), story counters, visited save points, unlocked teleporters and the party checkpoint. Game thread only.
#pragma once

namespace qd::sync {

void WorldSyncInit();       // message handler + ticker; call after coop::Init
void WorldSyncShutdown();
// True while an event received from another machine runs here (its loot rolls are skipped: the drop comes from there).
bool WorldSyncReplaying();
// Back at the main menu (a new game or another save comes next): forget the shared world of the last game, so it is
// not written into the next one. The host sends its new world to everyone once it has loaded.
void WorldSyncNewRound();
// Game over: the whole party is down and dies. The reload that follows takes the host's save as the world for
// everyone (what was looted or opened since that save is back), instead of keeping the shared world.
void WorldSyncPartyWiped();
// Client: ask the host for its whole world again and write it into this game (the F5 Fix tab).
void WorldSyncRequestFull();
// True when a received event ran on this object in the last `seconds` (a pot broken by another player rolls its loot
// at the end of its break animation, after the replay itself has returned).
bool WorldSyncReplayedRecently(const void* obj, double seconds);
// 1.6.11: the pickup was already picked up by someone (its GUID is in the shared "taken" list)
bool WorldSyncPickupTaken(const void* pickup);

}  // namespace qd::sync
