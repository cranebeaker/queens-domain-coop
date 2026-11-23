// Knights' Domain: avatar looks (see Look.h).
#include "qdcoop/sync/Look.h"

#include "qdcoop/core/Config.h"
#include "qdcoop/core/Core.h"
#include "qdcoop/core/Log.h"
#include "qdcoop/net/Protocol.h"
#include "qdcoop/sync/Coop.h"

#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>

namespace qd::sync {
namespace {

constexpr const char* kTag = "look";

std::atomic<int> g_local{0};
std::array<int, net::kMaxPlayers> g_heads{-1, -1, -1, -1};
bool g_wasInSession = false;

std::filesystem::path File() { return std::filesystem::path(paths::ModSaved()) / L"look.txt"; }

void Load() {
    std::ifstream f(File());
    int h = 0;
    if (f >> h && h >= 0 && h < net::kLookHeads) g_local = h;
}

void Save() {
    std::error_code ec;
    std::filesystem::create_directories(File().parent_path(), ec);
    std::ofstream f(File(), std::ios::trunc);
    f << g_local.load() << "\n";
}

void SendOne(uint8_t slot, int head, int toSlot /* -1 = everyone */) {
    if (head < 0) return;
    net::MsgLook m;
    m.slot = slot;
    m.head = static_cast<uint8_t>(head);
    const auto bytes = net::Encode(m);
    if (toSlot >= 0) coop::SendToSlot(static_cast<uint8_t>(toSlot), net::Channel::Reliable, bytes);
    else coop::Broadcast(net::Channel::Reliable, bytes);   // host: to all clients; client: to the host
}

void SendMine() {
    auto slot = coop::LocalSlot();
    if (!slot || !coop::InSession()) return;
    g_heads[*slot] = g_local.load();
    SendOne(*slot, g_local.load(), -1);
}

void OnLook(uint8_t fromSlot, const uint8_t* data, size_t size) {
    net::MsgLook m;
    if (!net::Decode(data, size, m)) return;
    if (coop::IsHost()) {
        if (fromSlot == 0 || fromSlot >= net::kMaxPlayers) return;
        m.slot = fromSlot;   // never trust the slot a client claims
        g_heads[m.slot] = m.head;
        for (const auto& mem : coop::Members())
            if (mem.present && !mem.isLocal && mem.slot != fromSlot) SendOne(m.slot, m.head, mem.slot);
        return;
    }
    if (fromSlot != 0) return;
    g_heads[m.slot] = m.head;
}

void OnMember(const coop::Member& mem, bool joined) {
    if (mem.slot < net::kMaxPlayers && !joined) g_heads[mem.slot] = -1;
    if (!joined || mem.isLocal || !coop::IsHost()) return;
    // the newcomer gets everyone's look, the host's own included
    for (const auto& m : coop::Members())
        if (m.present && m.slot != mem.slot) SendOne(m.slot, m.isLocal ? g_local.load() : g_heads[m.slot], mem.slot);
}

void Tick(float) {
    const bool in = coop::InSession();
    if (in && !g_wasInSession) SendMine();
    if (!in && g_wasInSession) g_heads.fill(-1);
    g_wasInSession = in;
}

}  // namespace

int LookLocalHead() { return g_local.load(); }

void LookSetLocalHead(int head) {
    if (head < 0 || head >= net::kLookHeads) return;
    g_local = head;
    gamethread::Post([] {
        Save();
        SendMine();
    });
}

int LookHeadOf(uint8_t slot) { return slot < net::kMaxPlayers ? g_heads[slot] : -1; }

void LookInit() {
    Load();
    coop::On(net::MsgType::Look, OnLook);
    coop::AddMemberListener(OnMember);
    gamethread::AddTicker("look", [](float dt) { Tick(dt); });
    QD_INFO(kTag, "initialized (head {})", g_local.load());
}

}  // namespace qd::sync
