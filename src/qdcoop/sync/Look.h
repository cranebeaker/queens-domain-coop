// Knights' Domain: how each player's avatar looks to the others (the knight's head, picked in the co-op lobby). Kept in
// ModSaved()\look.txt; sent with net::MsgLook (host relays, a newcomer gets everyone's). Game thread unless noted.
#pragma once
#include <cstdint>

namespace qd::sync {

void LookInit();          // after coop::Init: message handler, member listener, ticker
int  LookLocalHead();     // thread-safe
void LookSetLocalHead(int head);   // thread-safe: saved, and sent to the others
int  LookHeadOf(uint8_t slot);     // -1 when that player has not said

}  // namespace qd::sync
