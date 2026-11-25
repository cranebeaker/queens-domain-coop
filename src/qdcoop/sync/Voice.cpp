// Knights' Domain: proximity voice chat (see Voice.h).
//
// Every frame (game thread): the session follows the co-op session (host or client, own slot, who is in), the microphone's
// 20 ms frames go to the transmitter with the talk key's state, and every remote player's place relative to the local
// player (from the same interpolated poses the puppets use) sets their volume and side. The speakers pull the mix on their
// own thread. Voice datagrams are net::MsgType::Voice followed by the voice wire bytes, unreliable; the host relays.
//
// Include order: SDK headers FIRST (see GameQueries.cpp for why).
#include "SDK/Engine_classes.hpp"
#include "SDK/FS_PlayerChar_classes.hpp"

#include "qdcoop/sync/Voice.h"

#include "qdcoop/core/Config.h"
#include "qdcoop/core/Core.h"
#include "qdcoop/core/Log.h"
#include "qdcoop/core/Unreal.h"
#include "qdcoop/net/Protocol.h"
#include "qdcoop/sync/Coop.h"
#include "qdcoop/sync/EnemySync.h"
#include "qdcoop/sync/PlayerSync.h"
#include "qdcoop/ui/I18n.h"
#include "qdcoop/ui/Overlay.h"

#include "voice_audio_win.h"
#include "voice_codec.h"
#include "voice_config.h"
#include "voice_session.h"

#include "imgui.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <set>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace qd::sync {
namespace {

using namespace nvcoop::voice;
constexpr const char* kTag = "voice";

VoiceSettings g_set;
std::mutex g_mtx;   // the session and what the speaker thread reads
std::unique_ptr<IVoiceCodecFactory> g_codecs;
bool g_codecFailed = false;
std::unique_ptr<VoiceSession> g_session;
bool g_isHost = false;
uint8_t g_self = 0xFF;
bool g_wasIn[kMaxSlots] = {};
bool g_rebuild = false;   // settings changed: the session is built again

PcmQueue g_queue;
AudioCapture g_capture;
AudioPlayback g_playback;

// dev / test controls
bool g_forceKey = false, g_tone = false;
uint64_t g_toneSamples = 0, g_toneLastMs = 0;

// what the F5 tab and the indicator show (written on the game thread, read on the render thread)
struct View {
    bool on = false, inSession = false, talking = false, micFailed = false, speakersFailed = false;
    float level = 0;
    std::array<bool, kMaxSlots> heard{}, present{}, muted{};
    std::array<float, kMaxSlots> gain{};
    std::array<std::string, kMaxSlots> names;
};
std::mutex g_viewMx;
View g_view;

std::mutex g_devMx;
std::vector<std::string> g_mics, g_speakers;
double g_devNext = 0;
void RefreshDevices() {
    const double now = static_cast<double>(GetTickCount64()) / 1000.0;
    if (now < g_devNext) return;
    g_devNext = now + 5.0;
    auto mics = ListDevices(true), spk = ListDevices(false);
    std::lock_guard lk(g_devMx);
    g_mics = std::move(mics);
    g_speakers = std::move(spk);
}

// the F5 tab's "Test microphone": the microphone runs outside a session so the meter moves
std::atomic<bool> g_micTest{false};
bool g_testRunning = false;

uint64_t NowMs() { return static_cast<uint64_t>(GetTickCount64()); }

std::filesystem::path IniPath() { return std::filesystem::path(paths::ModSaved()) / L"voice.ini"; }

IniGetter Getter() {
    const std::wstring ini = IniPath().wstring();
    return [ini](const char* section, const char* key, const char* def) -> std::string {
        wchar_t buf[256] = {};
        const std::wstring s(section, section + std::strlen(section)), k(key, key + std::strlen(key)), d(def, def + std::strlen(def));
        GetPrivateProfileStringW(s.c_str(), k.c_str(), d.c_str(), buf, 256, ini.c_str());
        std::string out;
        for (const wchar_t* p = buf; *p; ++p) out += static_cast<char>(*p < 128 ? *p : '?');
        return out;
    };
}

// Writes one setting and reads them all again (the next frame rebuilds the session with them).
void SetSetting(const char* key, const std::string& value) {
    std::error_code ec;
    std::filesystem::create_directories(IniPath().parent_path(), ec);
    const std::wstring k(key, key + std::strlen(key)), v(value.begin(), value.end());
    WritePrivateProfileStringW(L"voice", k.c_str(), v.c_str(), IniPath().wstring().c_str());
    g_set = readSettings(Getter());
    g_rebuild = true;
    if (g_testRunning) {   // the test microphone follows the new device
        g_capture.stop();
        g_testRunning = false;
    }
}

// ---- where everyone is: Unreal (cm, yaw in degrees, X forward / Y right) to the placement's convention (yaw in radians,
// right-hand side = (cos yaw, -sin yaw))

float PlacementYaw(float ueYawDeg) { return -(ueYawDeg * 3.14159265f / 180.0f + 1.5707963f); }

Where Listener() {
    Where w;
    SDK::ACharacter* ch = ue::LocalCharacter();
    if (!ch || !ue::IsValid(ch)) return w;
    const SDK::FVector p = ch->K2_GetActorLocation();
    float yaw = ch->K2_GetActorRotation().Yaw;
    if (SDK::APlayerController* pc = ue::LocalController(); pc && ue::IsValid(pc)) yaw = pc->ControlRotation.Yaw;
    w.present = true;
    w.pos = Vec3{p.X, p.Y, p.Z};
    w.yaw = PlacementYaw(yaw);
    return w;
}

Where Speaker(uint8_t slot) {
    Where w;
    net::Vec3f feet{};
    float yaw = 0;
    if (!playersync::RemotePose(slot, feet, yaw)) return w;   // not here, another area, or no pose
    w.present = true;
    w.pos = Vec3{feet.x, feet.y, feet.z + 90.0f};   // about the middle of the body, like the listener's actor location
    return w;
}

// ---- session

void Teardown() {
    g_capture.stop();
    g_playback.stop();
    {
        std::lock_guard lk(g_mtx);
        g_session.reset();
    }
    g_self = 0xFF;
    std::fill(std::begin(g_wasIn), std::end(g_wasIn), false);
    g_queue.clear();
}

void SendDatagram(uint8_t slot, const Bytes& b) {
    std::vector<uint8_t> msg;
    msg.reserve(b.size() + 1);
    msg.push_back(static_cast<uint8_t>(net::MsgType::Voice));
    msg.insert(msg.end(), b.begin(), b.end());
    if (coop::IsHost()) coop::SendToSlot(slot, net::Channel::Unreliable, msg);
    else coop::SendToHost(net::Channel::Unreliable, msg);
}

std::vector<uint8_t> Clients() {
    std::vector<uint8_t> v;
    for (const auto& m : coop::Members())
        if (m.present && !m.isLocal && m.slot < kMaxSlots) v.push_back(m.slot);
    return v;
}

bool Build(bool isHost, uint8_t self) {
    if (!g_codecs && !g_codecFailed) {
        g_codecs = makeOpusFactory(g_set.bitrate);
        if (!g_codecs) {
            g_codecFailed = true;
            QD_WARN(kTag, "the speech codec did not start; voice chat is off for this run");
        }
    }
    if (!g_codecs) return false;
    SessionConfig cfg;
    cfg.isHost = isHost;
    cfg.self = self;
    cfg.tx = txConfigFrom(g_set);
    cfg.jitter = jitterConfigFrom(g_set);
    {
        std::lock_guard lk(g_mtx);
        g_session = std::make_unique<VoiceSession>(cfg, *g_codecs, SendDatagram, Clients);
        g_session->rx().setMasterVolume(g_set.volume);
        for (uint8_t s : g_set.muted) g_session->rx().setMuted(s, true);
    }
    g_isHost = isHost;
    g_self = self;
    g_queue.clear();
    g_capture.start(g_set.micDevice, &g_queue);
    g_playback.start(g_set.speakerDevice, [](int16_t* stereo, size_t frames) {
        std::lock_guard lk(g_mtx);
        if (g_session) g_session->render(stereo, frames);
        else std::memset(stereo, 0, frames * 2 * sizeof(int16_t));
    });
    QD_INFO(kTag, "voice chat up as {} slot {} ({})", isHost ? "host" : "client", self, describe(g_set));
    return true;
}

void OnVoice(uint8_t fromSlot, const uint8_t* data, size_t size) {
    if (size < 2) return;
    std::lock_guard lk(g_mtx);
    if (g_session) g_session->onDatagram(fromSlot, data + 1, size - 1, NowMs());
}

bool TalkKeyDown() {
    if (g_set.key <= 0) return false;
    HWND fg = GetForegroundWindow();
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    if (pid != GetCurrentProcessId()) return false;   // only while the game has the focus
    return (GetAsyncKeyState(g_set.key) & 0x8000) != 0;
}

void ToneFrame(int16_t* out) {
    for (size_t i = 0; i < kFrameSamples; ++i) {
        const double t = static_cast<double>(g_toneSamples++) / kSampleRate;
        out[i] = static_cast<int16_t>(8000.0 * std::sin(6.283185307179586 * 440.0 * t));
    }
}

void Tick(float) {
    if (ui::IsMenuOpen()) RefreshDevices();   // only while F5 is open: the lists are for the Voice tab
    const bool in = g_set.enabled && coop::InSession();
    const auto local = coop::LocalSlot();
    if (!in || !local || *local >= kMaxSlots) {
        if (g_session || g_self != 0xFF) Teardown();
        // "Test microphone": loudness of the microphone, as the transmitter would measure it
        float level = 0;
        bool failed = false;
        if (g_micTest.load() && g_set.enabled) {
            if (!g_testRunning) {
                g_queue.clear();
                g_capture.start(g_set.micDevice, &g_queue);
                g_testRunning = true;
            }
            int16_t frame[kFrameSamples];
            while (g_queue.popFrame(frame)) {
                double sum = 0;
                for (size_t i = 0; i < kFrameSamples; ++i) sum += std::abs(static_cast<int>(frame[i]));
                level = static_cast<float>(sum / kFrameSamples) * g_set.micVolume;
            }
            failed = g_capture.failed();
        } else if (g_testRunning) {
            g_capture.stop();
            g_testRunning = false;
        }
        std::lock_guard lk(g_viewMx);
        const float last = g_view.level;
        g_view = View{};
        g_view.on = g_set.enabled;
        g_view.level = g_testRunning ? (level > 0 ? level : last * 0.9f) : 0;
        g_view.micFailed = failed;
        return;
    }
    if (g_testRunning) {   // a session takes the microphone over
        g_capture.stop();
        g_testRunning = false;
        g_micTest = false;
    }
    const bool host = coop::IsHost();
    if (!g_session || g_rebuild || host != g_isHost || *local != g_self) {
        g_rebuild = false;
        if (g_session) Teardown();
        if (!Build(host, *local)) return;
    }
    // a player who left: their buffer and decoder go
    bool inNow[kMaxSlots] = {};
    for (uint8_t s : Clients()) inNow[s] = true;
    if (!host) inNow[0] = true;
    for (uint8_t s = 0; s < kMaxSlots; ++s) {
        if (g_wasIn[s] && !inNow[s]) {
            std::lock_guard lk(g_mtx);
            if (g_session) g_session->playerLeft(s);
        }
        g_wasIn[s] = inNow[s];
    }

    const uint64_t now = NowMs();
    const bool keyDown = g_forceKey || TalkKeyDown();
    const Where me = Listener();
    const PlacementConfig pc = placementConfigFrom(g_set);
    Placement places[kMaxSlots];
    View v;
    v.on = true;
    v.inSession = true;
    for (const auto& m : coop::Members())
        if (m.present && m.slot < kMaxSlots) v.names[m.slot] = m.name;
    for (uint8_t slot = 0; slot < kMaxSlots; ++slot) {
        if (slot == *local) continue;
        const Where them = Speaker(slot);
        places[slot] = computePlacement(me, them, pc);
        v.present[slot] = them.present;
    }
    {
        std::lock_guard lk(g_mtx);
        if (!g_session) return;
        int16_t frame[kFrameSamples];
        int fed = 0;
        if (g_tone) {
            g_queue.clear();
            uint64_t due = g_toneLastMs ? (now - g_toneLastMs) / kFrameMs : 1;
            if (due > 5) due = 5;
            g_toneLastMs = g_toneLastMs ? g_toneLastMs + due * kFrameMs : now;
            for (uint64_t i = 0; i < due; ++i) {
                ToneFrame(frame);
                g_session->feedMic(frame, keyDown, false);
            }
        } else {
            g_toneLastMs = 0;
            while (fed < 6 && g_queue.popFrame(frame)) {
                g_session->feedMic(frame, keyDown, false);
                ++fed;
            }
        }
        for (uint8_t slot = 0; slot < kMaxSlots; ++slot) g_session->rx().setPlacement(slot, places[slot]);
        v.talking = g_session->tx().talking();
        v.level = static_cast<float>(g_session->tx().level());
        for (uint8_t slot = 0; slot < kMaxSlots; ++slot) {
            if (slot == *local) continue;
            v.heard[slot] = g_session->rx().talking(slot);
            v.gain[slot] = g_session->rx().gainOf(slot);
            v.muted[slot] = g_session->rx().muted(slot);
        }
    }
    v.micFailed = g_capture.failed();
    v.speakersFailed = g_playback.failed();
    std::lock_guard lk(g_viewMx);
    g_view = std::move(v);
}

View ViewNow() {
    std::lock_guard lk(g_viewMx);
    return g_view;
}

// ---- the talking indicator: bottom left, who is talking (you included)

void DrawIndicator() {
    const View v = ViewNow();
    if (!v.inSession) return;
    std::vector<std::string> lines;
    if (v.talking) lines.push_back(ui::Tr("voice.you_talk"));
    for (uint8_t s = 0; s < kMaxSlots; ++s)
        if (v.heard[s] && !v.muted[s]) lines.push_back(ui::TrF("voice.talking", v.names[s].empty() ? std::string("?") : v.names[s]));
    if (v.micFailed && v.talking) lines.push_back(ui::Tr("voice.no_mic"));
    if (lines.empty()) return;
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    float y = io.DisplaySize.y * 0.72f;
    for (const auto& l : lines) {
        const ImVec2 sz = ImGui::CalcTextSize(l.c_str());
        const ImVec2 p0(24.0f, y), p1(24.0f + sz.x + 34.0f, y + sz.y + 12.0f);
        dl->AddRectFilled(p0, p1, IM_COL32(18, 13, 22, 200));
        dl->AddCircleFilled(ImVec2(p0.x + 13.0f, p0.y + (sz.y + 12.0f) * 0.5f), 5.0f, IM_COL32(110, 220, 120, 255));
        dl->AddText(ImVec2(p0.x + 26.0f, p0.y + 6.0f), IM_COL32(245, 234, 210, 255), l.c_str());
        y += sz.y + 18.0f;
    }
}

// ---- F5 > Voice

// A slider showing `saved * scale`; while it is being dragged it keeps its own value (re-reading the setting every frame
// snapped it back), and when it is let go the new value goes to voice.ini.
void SettingSlider(const char* label, const char* key, float saved, float scale, float lo, float hi, const char* fmt, float width,
                   ImGuiSliderFlags flags = 0) {
    static std::map<std::string, float> held;   // render thread only
    const std::string k = key;
    auto it = held.find(k);
    float v = it != held.end() ? it->second : saved * scale;
    ImGui::SetNextItemWidth(width);
    ImGui::SliderFloat(label, &v, lo, hi, fmt, flags);
    if (ImGui::IsItemActive()) held[k] = v;
    if (ImGui::IsItemDeactivated()) {
        held.erase(k);
        const std::string val = std::to_string(v / scale);
        gamethread::Post([k, val] { SetSetting(k.c_str(), val); });
    }
}

const int kKeys[] = {0xA4 /*LAlt*/, 'V', 'B', 'T', 'X', 0x14 /*Caps*/, 0x05 /*Mouse4*/, 0x06 /*Mouse5*/};

void DrawTab() {
    const View v = ViewNow();
    const VoiceSettings s = g_set;   // read on the render thread: plain values, changed only through SetSetting posts
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", ui::Tr("voice.help"));
    ImGui::PopTextWrapPos();
    ImGui::Spacing();

    bool enabled = s.enabled;
    if (ImGui::Checkbox(ui::Tr("voice.enabled"), &enabled)) gamethread::Post([enabled] { SetSetting("enabled", enabled ? "1" : "0"); });

    int mode = s.mode == Mode::OpenMic ? 1 : 0;
    ImGui::SeparatorText(ui::Tr("voice.mic_section"));
    ImGui::TextUnformatted(ui::Tr("voice.mode"));
    ImGui::SameLine();
    if (ImGui::RadioButton(ui::Tr("voice.ptt"), mode == 0) && mode != 0) gamethread::Post([] { SetSetting("mode", "ptt"); });
    ImGui::SameLine();
    if (ImGui::RadioButton(ui::Tr("voice.open"), mode == 1) && mode != 1) gamethread::Post([] { SetSetting("mode", "open"); });

    // which microphone and which speakers (Windows default unless one is picked)
    {
        std::vector<std::string> mics, spk;
        {
            std::lock_guard lk(g_devMx);
            mics = g_mics;
            spk = g_speakers;
        }
        auto picker = [](const char* label, const char* id, const char* key, int cur, const std::vector<std::string>& list) {
            ImGui::TextUnformatted(label);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(340.0f);
            const std::string shown = cur >= 0 && cur < static_cast<int>(list.size()) ? list[cur] : std::string(ui::Tr("voice.default_device"));
            if (ImGui::BeginCombo(id, shown.c_str())) {
                if (ImGui::Selectable(ui::Tr("voice.default_device"), cur < 0)) {
                    const std::string k = key;
                    gamethread::Post([k] { SetSetting(k.c_str(), "-1"); });
                }
                for (int i = 0; i < static_cast<int>(list.size()); ++i) {
                    ImGui::PushID(i);
                    if (ImGui::Selectable(list[i].c_str(), i == cur)) {
                        const std::string k = key, v = std::to_string(i);
                        gamethread::Post([k, v] { SetSetting(k.c_str(), v); });
                    }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
        };
        picker(ui::Tr("voice.mic_device"), "##micdev", "mic_device", s.micDevice, mics);
        picker(ui::Tr("voice.speaker_device"), "##spkdev", "speaker_device", s.speakerDevice, spk);
    }
    // the meter: how loud the microphone is now; in voice activation it is also the sensitivity slider: drag the
    // gold mark (or click anywhere on the bar) to set where it starts sending
    {
        constexpr float kMax = 4000.0f;   // loudness at the right end (a shout is about 3000)
        auto pos = [](float v) { return std::sqrt(std::clamp(v, 0.0f, kMax) / kMax); };
        const float w = 340.0f, h = 18.0f;
        ImGui::TextUnformatted(ui::Tr("voice.mic_level"));
        ImGui::SameLine();
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        static float heldThreshold = -1.0f;   // render thread only: the value while the mark is dragged
        ImGui::InvisibleButton("##voicemeter", ImVec2(w, h + 6.0f));
        float threshold = heldThreshold > 0 ? heldThreshold : s.openThreshold;
        if (mode == 1) {
            if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            if (ImGui::IsItemActive()) {
                const float f = std::clamp((ImGui::GetIO().MousePos.x - p0.x) / w, 0.0f, 1.0f);
                threshold = std::clamp(f * f * kMax, 50.0f, kMax);
                heldThreshold = threshold;
                ImGui::SetTooltip("%s %.0f", ui::Tr("voice.threshold"), threshold);
            }
            if (ImGui::IsItemDeactivated() && heldThreshold > 0) {
                const std::string val = std::to_string(heldThreshold);
                heldThreshold = -1.0f;
                gamethread::Post([val] { SetSetting("open_threshold", val); });
            }
        }
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p1(p0.x + w, p0.y + h);
        dl->AddRectFilled(p0, p1, IM_COL32(28, 24, 30, 255));
        const bool open = mode == 1 && v.level >= threshold;
        const bool sending = v.talking || open;
        dl->AddRectFilled(p0, ImVec2(p0.x + w * pos(v.level), p1.y), sending ? IM_COL32(110, 210, 120, 255) : IM_COL32(150, 140, 150, 255));
        if (mode == 1) {
            const float x = p0.x + w * pos(threshold);
            dl->AddLine(ImVec2(x, p0.y - 3.0f), ImVec2(x, p1.y + 3.0f), IM_COL32(240, 200, 90, 255), 2.5f);
            dl->AddTriangleFilled(ImVec2(x - 6.0f, p1.y + 7.0f), ImVec2(x + 6.0f, p1.y + 7.0f), ImVec2(x, p1.y + 1.0f), IM_COL32(240, 200, 90, 255));
        }
        dl->AddRect(p0, p1, IM_COL32(120, 100, 70, 255));
        ImGui::SameLine();
        ImGui::TextColored(sending ? ImVec4(0.45f, 0.85f, 0.5f, 1.0f) : ImVec4(0.6f, 0.58f, 0.62f, 1.0f), "%s",
                           ui::Tr(sending ? "voice.sending" : "voice.quiet"));
        if (!v.inSession) {
            bool test = g_micTest.load();
            if (ImGui::Checkbox(ui::Tr("voice.test_mic"), &test)) g_micTest = test;
            ImGui::SameLine();
            ImGui::TextDisabled("%s", ui::Tr("voice.test_help"));
        }
    }
    if (mode == 1) ImGui::TextDisabled("%s", ui::Tr("voice.threshold_help"));

    ImGui::TextUnformatted(ui::Tr(mode == 1 ? "voice.key_also" : "voice.key"));
    ImGui::SameLine();
    const std::string cur = keyName(s.key);
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::BeginCombo("##voicekey", cur.c_str())) {
        for (int vk : kKeys) {
            const std::string n = keyName(vk);
            if (ImGui::Selectable(n.c_str(), vk == s.key)) gamethread::Post([n] { SetSetting("key", n); });
        }
        ImGui::EndCombo();
    }

    SettingSlider(ui::Tr("voice.volume"), "volume", s.volume, 100.0f, 0.0f, 200.0f, "%.0f %%", 260.0f);
    SettingSlider(ui::Tr("voice.mic_volume"), "mic_volume", s.micVolume, 100.0f, 0.0f, 400.0f, "%.0f %%", 260.0f);
    SettingSlider(ui::Tr("voice.range"), "range", s.range, 0.01f, 5.0f, 100.0f, "%.0f m", 260.0f);

    if (!v.inSession) {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", ui::Tr("voice.not_in_session"));
        return;
    }
    ImGui::SeparatorText(ui::Tr("voice.players"));
    if (v.micFailed) ImGui::TextColored(ImVec4(0.93f, 0.42f, 0.45f, 1.0f), "%s", ui::Tr("voice.no_mic"));
    if (v.speakersFailed) ImGui::TextColored(ImVec4(0.93f, 0.42f, 0.45f, 1.0f), "%s", ui::Tr("voice.no_speakers"));
    ImGui::Spacing();
    // the others: heard or not, how loud, mute
    for (uint8_t slot = 0; slot < kMaxSlots; ++slot) {
        if (v.names[slot].empty() || slot == coop::LocalSlot().value_or(0xFF)) continue;
        ImGui::PushID(slot);
        bool muted = v.muted[slot];
        if (ImGui::Checkbox(ui::Tr("voice.mute"), &muted)) {
            const bool m = muted;
            const uint8_t sl = slot;
            gamethread::Post([sl, m] {
                std::string list;
                std::set<uint8_t> set = g_set.muted;
                if (m) set.insert(sl);
                else set.erase(sl);
                for (uint8_t x : set) list += (list.empty() ? "" : ",") + std::to_string(x);
                SetSetting("muted", list);
            });
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(v.names[slot].c_str());
        ImGui::SameLine();
        if (!v.present[slot]) ImGui::TextDisabled("%s", ui::Tr("voice.far"));
        else if (v.heard[slot]) ImGui::TextColored(ImVec4(0.55f, 0.86f, 0.62f, 1.0f), "%s", ui::Tr("voice.talking_short"));
        else ImGui::TextDisabled("%s", ui::TrF("voice.volume_at", static_cast<int>(v.gain[slot] * 100.0f)).c_str());
        ImGui::PopID();
    }
}

}  // namespace

void VoiceInit() {
    g_set = readSettings(Getter());
    if (!g_set.warnings.empty()) QD_WARN(kTag, "voice.ini: {}", g_set.warnings);
    // Queen's Domain is in centimetres: unless voice.ini says otherwise, full volume within 4 m, silent past 30 m
    {
        wchar_t buf[32] = {};
        GetPrivateProfileStringW(L"voice", L"range", L"", buf, 32, IniPath().wstring().c_str());
        if (!buf[0]) g_set.range = 3000.0f;
        GetPrivateProfileStringW(L"voice", L"full_range", L"", buf, 32, IniPath().wstring().c_str());
        if (!buf[0]) g_set.fullRange = 400.0f;
    }
    coop::On(net::MsgType::Voice, OnVoice);
    gamethread::AddTicker("voice", [](float dt) { Tick(dt); });
    ui::AddMenuTab("voice", "tab.voice", DrawTab, 40);
    ui::AddHudDrawer("voice", DrawIndicator);
    QD_INFO(kTag, "initialized ({})", describe(g_set));
}

void VoiceShutdown() { Teardown(); }

#if QD_DEV
void VoiceDebug(const std::string& args) {
    if (args.rfind("tone", 0) == 0) g_tone = args.find('1') != std::string::npos;
    else if (args.rfind("key", 0) == 0) g_forceKey = args.find('1') != std::string::npos;
    const View v = ViewNow();
    std::string heard;
    for (uint8_t s = 0; s < kMaxSlots; ++s)
        heard += std::string(s ? "," : "") + "{\"heard\":" + (v.heard[s] ? "true" : "false") + ",\"gain\":" + std::to_string(v.gain[s]) +
                 ",\"present\":" + (v.present[s] ? "true" : "false") + "}";
    uint64_t sent = 0, received = 0, relayed = 0, played = 0;
    {
        std::lock_guard lk(g_mtx);
        if (g_session) {
            sent = g_session->stats().sent;
            received = g_session->stats().received;
            relayed = g_session->stats().relayed;
            played = g_session->stats().played;
        }
    }
    QD_INFO(kTag, "voice status {{\"on\":{},\"talking\":{},\"tone\":{},\"key\":{},\"sent\":{},\"received\":{},\"relayed\":{},\"played\":{},"
                  "\"mic\":{},\"speakers\":{},\"slots\":[{}]}}",
            v.inSession, v.talking, g_tone, g_forceKey, sent, received, relayed, played, !v.micFailed, !v.speakersFailed, heard);
}
#endif

}  // namespace qd::sync
