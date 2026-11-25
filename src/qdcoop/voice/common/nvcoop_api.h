// Knights' Domain: the one thing the voice code (ported from the Fallout co-op project) needs from its old host, the log.
#pragma once

namespace nvcoop {

void Log(const char* fmt, ...);   // printf style; goes to KnightsDomain.log with the tag "voice"

}  // namespace nvcoop
