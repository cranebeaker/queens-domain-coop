// Knights' Domain: proximity voice chat. The voice pipeline (microphone, Opus, jitter buffer, positional mix, speakers) is
// the Fallout co-op project's (qdcoop/voice, docs there); this module is the Queen's Domain glue: the session on the co-op
// connection (net::MsgType::Voice, unreliable), where everyone stands, the talk key, the F5 "Voice" tab and the talking
// indicator. Settings: ModSaved()\voice.ini, section [voice]. Game thread unless noted.
#pragma once
#include <string>

namespace qd::sync {

void VoiceInit();       // after coop::Init + ui::Init
void VoiceShutdown();

#if QD_DEV
// Dev console "voice ...": tone 0|1 (a test tone instead of the microphone), key 0|1 (hold the talk key), status (one
// "voice status {...}" JSON line: what is sent, heard and how loud each player is).
void VoiceDebug(const std::string& args);
#endif

}  // namespace qd::sync
