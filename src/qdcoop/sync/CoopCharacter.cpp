// Knights' Domain: the host keeps every friend's character (see CoopCharacter.h).
//
// How the game saves (from its Blueprints): a slot's file name comes from BlueprintUtils.GetSaveSlotName(slot, ...) and
// is written next to the system save under Saved\SaveGames; TitleScreenUI."Load Slot"(slot) stores the slot number,
// has the game instance load it and opens the level; a slot with no file starts a new game (that is all "New Game"
// does). So a friend's co-op character is just the file of kCoopSlot: copied from the host before "Load Slot", and
// copied back to the host whenever the friend's game writes it (its main file or a suspend save, the newest one).
//
// Include order: SDK headers FIRST (see GameQueries.cpp for why).
#include "SDK/Engine_classes.hpp"
#include "SDK/Engine_parameters.hpp"
#include "SDK/QueensDomain_classes.hpp"
#include "SDK/QueensDomain_parameters.hpp"
#include "SDK/SaveFunctions_classes.hpp"
#include "SDK/SaveFunctions_parameters.hpp"
#include "SDK/FS_SystemSave_classes.hpp"
#include "SDK/TitleScreenUI_classes.hpp"
#include "SDK/TitleScreenUI_parameters.hpp"
#include "SDK/FS_Button_classes.hpp"

#include "qdcoop/sync/CoopCharacter.h"

#include "qdcoop/core/Config.h"
#include "qdcoop/core/Core.h"
#include "qdcoop/core/Log.h"
#include "qdcoop/core/Unreal.h"
#include "qdcoop/net/Protocol.h"
#include "qdcoop/sync/Coop.h"
#include "qdcoop/sync/CoopInternal.h"
#include "qdcoop/sync/GameQueries.h"
#include "qdcoop/sync/Summon.h"
#include "qdcoop/ui/I18n.h"
#include "qdcoop/ui/Overlay.h"

#include <Windows.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace qd::sync {
namespace {

namespace fs = std::filesystem;
constexpr const char* kTag = "character";
constexpr const char* kTitleClass = "/Game/UI/Title/TitleScreenUI.TitleScreenUI_C";
constexpr const char* kSaveFunctionsClass = "/Game/Blueprints/SaveSystem/SaveFunctions.SaveFunctions_C";
constexpr size_t kChunk = 1000;
constexpr int kSuspendSlots = 4;          // suspend saves the game may keep per slot (index 0..3)
constexpr double kUploadCheckSec = 5.0;
constexpr double kTeleportAfterSec = 4.0;
constexpr double kReplyTimeoutSec = 20.0;

using Clock = std::chrono::steady_clock;
double Now() { return std::chrono::duration<double>(Clock::now().time_since_epoch()).count(); }

struct Incoming {   // one file arriving in chunks
    uint32_t id = 0, total = 0;
    std::vector<uint8_t> data;
    bool active = false;
};

enum class Boot { None, Asked, Ready, Loading };

struct State {
    bool inited = false;
    gamethread::TickerId ticker = 0;

    // client
    Boot boot = Boot::None;
    bool togetherFresh = false;
    double askedAt = 0;
    bool haveData = false;
    std::vector<uint8_t> data;   // the character from the host (empty: a fresh one)
    Incoming fromHost;
    bool playingCoop = false;    // the loaded game is the co-op character
    double inGameSince = -1;
    bool teleportPending = false;
    double nextUploadCheck = 0;
    double nextTitleCheck = 0;
    SDK::UTitleScreenUI_C* title = nullptr;
    fs::file_time_type lastSentTime{};
    std::optional<float> savedSelectedSlot;   // the friend's own last-picked slot, put back afterwards

    // host
    std::map<uint8_t, Incoming> uploads;   // slot -> save arriving from that friend
    uint32_t nextId = 1;
};
State& S() {
    static State s;
    return s;
}

template <class P>
bool Call(SDK::UObject* obj, const char* fn, P& params) {
    SDK::UFunction* f = obj ? ue::FindFunction(obj, fn) : nullptr;
    if (!f) {
        QD_WARN(kTag, "{} not found on {}", fn, obj ? ue::ClassName(obj) : std::string("null"));
        return false;
    }
    ue::CallFunction(obj, f, &params);
    return true;
}

SDK::UObject* SaveFunctions() {
    SDK::UClass* c = ue::FindClass(kSaveFunctionsClass);
    return c ? c->ClassDefaultObject : nullptr;
}

std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

// The file name (no folder, no ".sav") the game uses for a slot.
std::wstring SlotName(int slot, bool suspend, int suspendIndex) {
    SDK::UObject* cdo = SDK::UBlueprintUtils::GetDefaultObj();
    SDK::UFunction* f = cdo ? ue::FindFunction(cdo, "GetSaveSlotName") : nullptr;
    if (!f) return {};
    SDK::Params::BlueprintUtils_GetSaveSlotName p{};
    p.SlotIndex = slot;
    p.bForceDemoName = false;
    p.bBackup = false;
    p.bSuspend = suspend;
    p.SuspendIndex = suspendIndex;
    p.BackupIndex = 0;
    ue::CallFunction(cdo, f, &p);
    return p.ReturnValue.ToWString();
}

// The folder the game's saves are in: Saved\SaveGames, or its sub-folder that holds the system save.
fs::path SaveDir() {
    const fs::path root = fs::path(paths::GameSaved()) / L"SaveGames";
    std::error_code ec;
    fs::path best = root;
    fs::file_time_type bestTime{};
    for (auto it = fs::recursive_directory_iterator(root, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::wstring n = it->path().filename().wstring();
        if (n.rfind(L"System", 0) != 0 || it->path().extension() != L".sav") continue;
        const auto t = it->last_write_time(ec);
        if (best == root || t > bestTime) {
            best = it->path().parent_path();
            bestTime = t;
        }
    }
    return best;
}

std::vector<fs::path> CoopFiles() {
    std::vector<fs::path> out;
    const fs::path dir = SaveDir();
    if (std::wstring n = SlotName(kCoopSlot, false, 0); !n.empty()) out.push_back(dir / (n + L".sav"));
    for (int i = 0; i < kSuspendSlots; ++i)
        if (std::wstring n = SlotName(kCoopSlot, true, i); !n.empty()) out.push_back(dir / (n + L".sav"));
    return out;
}

bool ReadFile(const fs::path& p, std::vector<uint8_t>& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

bool WriteFileAtomic(const fs::path& p, const std::vector<uint8_t>& data) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    fs::path tmp = p;
    tmp += L".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!f) return false;
    }
    return MoveFileExW(tmp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

fs::path HostStore(uint64_t friendId, int hostSlot) {
    return fs::path(paths::ModSaved()) / L"characters" / (std::to_wstring(friendId) + L"_slot" + std::to_wstring(hostSlot) + L".sav");
}

std::optional<uint64_t> PeerOfSlot(uint8_t slot) {
    for (const auto& m : coop::Members())
        if (m.present && m.slot == slot) return static_cast<uint64_t>(m.peer);
    return std::nullopt;
}

SDK::UTitleScreenUI_C* FindTitle() {
    SDK::UClass* cls = ue::FindClass(kTitleClass);
    if (!cls) return nullptr;
    auto& objs = SDK::UObject::GObjects;
    for (int i = 0; i < objs->Num(); ++i) {
        SDK::UObject* o = objs->GetByIndex(i);
        if (o && !o->IsDefaultObject() && o->IsA(cls) && ue::IsValid(o)) {
            auto* t = static_cast<SDK::UTitleScreenUI_C*>(o);
            if (t->LoadGameButton && ue::IsValid(t->LoadGameButton)) return t;
        }
    }
    return nullptr;
}

SDK::UFS_SystemSave_C* SystemSave(SDK::UObject* ctx) {
    SDK::UObject* cdo = SaveFunctions();
    if (!cdo) return nullptr;
    SDK::Params::SaveFunctions_C_GetActiveSystemSave p{};
    p.__WorldContext = ctx;
    if (!Call(cdo, "GetActiveSystemSave", p)) return nullptr;
    return p.ActiveSave;
}

// ---- sending a file in chunks

void SendFile(uint8_t toSlot, uint8_t purpose, const std::vector<uint8_t>& bytes) {
    auto& s = S();
    const uint32_t id = s.nextId++;
    auto send = [&](const net::MsgCharacter& m) {
        const auto b = net::Encode(m);
        if (coop::IsHost()) coop::SendToSlot(toSlot, net::Channel::Reliable, b);
        else coop::SendToHost(net::Channel::Reliable, b);
    };
    net::MsgCharacter m;
    m.purpose = purpose;
    m.id = id;
    m.total = static_cast<uint32_t>(bytes.size());
    m.kind = net::CharacterKind::Begin;
    send(m);
    m.kind = net::CharacterKind::Chunk;
    for (size_t off = 0; off < bytes.size(); off += kChunk) {
        m.offset = static_cast<uint32_t>(off);
        m.data.assign(bytes.begin() + static_cast<std::ptrdiff_t>(off), bytes.begin() + static_cast<std::ptrdiff_t>(std::min(bytes.size(), off + kChunk)));
        send(m);
    }
    m.kind = net::CharacterKind::End;
    m.data.clear();
    m.offset = m.total;
    send(m);
}

// Returns true when the file is complete in `in.data`.
bool Collect(Incoming& in, const net::MsgCharacter& m) {
    switch (m.kind) {
        case net::CharacterKind::Begin:
            in = Incoming{};
            in.active = true;
            in.id = m.id;
            in.total = m.total;
            in.data.resize(m.total);
            return false;
        case net::CharacterKind::Chunk:
            if (!in.active || m.id != in.id || static_cast<size_t>(m.offset) + m.data.size() > in.data.size()) return false;
            std::copy(m.data.begin(), m.data.end(), in.data.begin() + m.offset);
            return false;
        case net::CharacterKind::End:
            if (!in.active || m.id != in.id) return false;
            in.active = false;
            return true;
        default:
            return false;
    }
}

// ---- client: boot into the co-op character

void LoadCoopSlot(SDK::UTitleScreenUI_C* title) {
    auto& s = S();
    SDK::UObject* cdo = SaveFunctions();
    // a clean slot: whatever an earlier session left in it (suspend saves, backups) must not win over the host's copy
    if (cdo) {
        SDK::Params::SaveFunctions_C_DeleteSaveSlot d{};
        d.Slot = kCoopSlot;
        d.__WorldContext = title;
        Call(cdo, "DeleteSaveSlot", d);
    }
    std::error_code ec;
    for (const auto& f : CoopFiles()) fs::remove(f, ec);
    if (s.haveData && !s.data.empty()) {
        const std::wstring name = SlotName(kCoopSlot, false, 0);
        const fs::path file = SaveDir() / (name + L".sav");
        if (name.empty() || !WriteFileAtomic(file, s.data)) {
            QD_WARN(kTag, "could not write the co-op character to {}; starting a fresh one", file.string());
        } else {
            QD_INFO(kTag, "co-op character from the host written to {} ({} bytes)", file.string(), s.data.size());
        }
    } else {
        QD_INFO(kTag, "no co-op character kept by the host yet: a fresh one");
    }
    if (SDK::UFS_SystemSave_C* sys = SystemSave(title)) s.savedSelectedSlot = sys->SelectedSaveSlot;
    SDK::Params::TitleScreenUI_C_Load_Slot p{};
    p.Slot_0 = kCoopSlot;
    p.Backup = false;
    p.BackupSlot = 0;
    s.boot = Boot::Loading;
    s.playingCoop = true;
    s.inGameSince = -1;
    s.teleportPending = !s.togetherFresh;
    s.lastSentTime = {};
    s.nextUploadCheck = Now() + kUploadCheckSec;
    Call(title, "Load Slot", p);
    ui::Toast(ui::Tr(s.haveData ? "char.loading" : "char.fresh"), ui::ToastKind::Info, 5.0f);
}

// ---- client: send the character back whenever the game wrote it

void UploadIfChanged() {
    auto& s = S();
    std::error_code ec;
    fs::path newest;
    fs::file_time_type t{};
    for (const auto& f : CoopFiles()) {
        if (!fs::is_regular_file(f, ec)) continue;
        const auto ft = fs::last_write_time(f, ec);
        if (ec) continue;
        if (newest.empty() || ft > t) {
            newest = f;
            t = ft;
        }
    }
    if (newest.empty() || t <= s.lastSentTime) return;
    // let the game finish writing: a file younger than 2 s waits for the next check
    if (fs::file_time_type::clock::now() - t < std::chrono::seconds(2)) return;
    std::vector<uint8_t> bytes;
    if (!ReadFile(newest, bytes) || bytes.empty() || bytes.size() > net::kMaxCharacterBytes) return;
    s.lastSentTime = t;
    SendFile(0, 1, bytes);
    QD_INFO(kTag, "co-op character sent to the host ({} bytes, from {})", bytes.size(), newest.filename().string());
}

// ---- messages

void OnCharacter(uint8_t fromSlot, const uint8_t* data, size_t size) {
    net::MsgCharacter m;
    if (!net::Decode(data, size, m)) return;
    auto& s = S();
    if (coop::IsHost()) {
        if (fromSlot == 0 || fromSlot >= net::kMaxPlayers) return;
        const auto peer = PeerOfSlot(fromSlot);
        if (!peer) return;
        int hostSlot = QueryActiveSaveSlot();   // live: "Load Slot" sets it at once
        if (hostSlot < 0) hostSlot = coop::ActiveSaveSlotCached();
        if (m.kind == net::CharacterKind::Request) {
            const fs::path p = HostStore(*peer, hostSlot);
            std::vector<uint8_t> bytes;
            std::error_code ec;
            if (fs::is_regular_file(p, ec) && ReadFile(p, bytes) && !bytes.empty()) {
                SendFile(fromSlot, 0, bytes);
                QD_INFO(kTag, "sent slot {}'s character ({} bytes, kept for host slot {})", fromSlot, bytes.size(), hostSlot);
            } else {
                net::MsgCharacter none;
                none.kind = net::CharacterKind::None;
                coop::SendToSlot(fromSlot, net::Channel::Reliable, net::Encode(none));
                QD_INFO(kTag, "slot {} has no character kept for host slot {} yet", fromSlot, hostSlot);
            }
            return;
        }
        if (m.purpose != 1) return;
        Incoming& in = s.uploads[fromSlot];
        if (!Collect(in, m)) return;
        if (!ue::LocalCharacter()) return;   // the host is not in a game: nothing to keep it for
        const fs::path p = HostStore(*peer, hostSlot);
        if (WriteFileAtomic(p, in.data)) QD_INFO(kTag, "kept slot {}'s character ({} bytes) for host slot {}", fromSlot, in.data.size(), hostSlot);
        else QD_WARN(kTag, "could not keep slot {}'s character in {}", fromSlot, p.string());
        in = Incoming{};
        return;
    }
    if (fromSlot != 0 || s.boot != Boot::Asked) return;
    if (m.kind == net::CharacterKind::None) {
        s.haveData = false;
        s.data.clear();
        s.boot = Boot::Ready;
        return;
    }
    if (m.purpose != 0) return;
    if (Collect(s.fromHost, m)) {
        s.data = std::move(s.fromHost.data);
        s.fromHost = Incoming{};
        s.haveData = true;
        s.boot = Boot::Ready;
        QD_INFO(kTag, "got this player's co-op character from the host ({} bytes)", s.data.size());
    }
}

void Tick(float) {
    auto& s = S();
    const double now = Now();
    const bool session = coop::InSession();
    const bool host = coop::IsHost();
    SDK::ACharacter* ch = ue::LocalCharacter();
    // the main menu is looked for four times a second, not every frame
    if (ch) {
        s.title = nullptr;
    } else if (now >= s.nextTitleCheck) {
        s.nextTitleCheck = now + 0.25;
        s.title = FindTitle();
    } else if (s.title && !ue::IsValid(s.title)) {
        s.title = nullptr;
    }
    SDK::UTitleScreenUI_C* title = s.title;

    if (!session || host) {
        if (s.boot != Boot::None) s.boot = Boot::None;
        if (!session) s.uploads.clear();
    }

    // back on the main menu: the friend's own last-picked slot comes back
    if (title && s.playingCoop && s.boot != Boot::Loading) {
        s.playingCoop = false;
        if (s.savedSelectedSlot)
            if (SDK::UFS_SystemSave_C* sys = SystemSave(title)) sys->SelectedSaveSlot = *s.savedSelectedSlot;
        s.savedSelectedSlot.reset();
    }

    if (s.boot == Boot::Asked && now - s.askedAt > kReplyTimeoutSec) {
        QD_WARN(kTag, "the host did not answer with a character in {:.0f} s; starting a fresh one", kReplyTimeoutSec);
        s.haveData = false;
        s.data.clear();
        s.boot = Boot::Ready;
    }
    if (s.boot == Boot::Ready && title) LoadCoopSlot(title);
    if (s.boot == Boot::Loading && ch) s.boot = Boot::None;

    if (!session || host || !ch || !s.playingCoop) {
        s.inGameSince = -1;
        return;
    }
    if (s.inGameSince < 0) s.inGameSince = now;
    if (s.teleportPending && now - s.inGameSince > kTeleportAfterSec) {
        s.teleportPending = false;
        SummonToHost();
    }
    if (now >= s.nextUploadCheck) {
        s.nextUploadCheck = now + kUploadCheckSec;
        UploadIfChanged();
    }
}

}  // namespace

void CharacterBoot(bool togetherFresh) {
    auto& s = S();
    if (coop::IsHost() || !coop::InSession()) return;
    if (s.boot == Boot::Asked || s.boot == Boot::Ready || s.boot == Boot::Loading) return;
    s.togetherFresh = togetherFresh;
    s.haveData = false;
    s.data.clear();
    s.fromHost = Incoming{};
    s.boot = Boot::Asked;
    s.askedAt = Now();
    net::MsgCharacter m;
    m.kind = net::CharacterKind::Request;
    coop::SendToHost(net::Channel::Reliable, net::Encode(m));
    QD_INFO(kTag, "asked the host for this player's co-op character");
}

bool CharacterBooting() {
    const Boot b = S().boot;
    return b == Boot::Asked || b == Boot::Ready || b == Boot::Loading;
}

void CharacterHostNewGame(int slot) {
    std::error_code ec;
    const fs::path dir = fs::path(paths::ModSaved()) / L"characters";
    const std::wstring suffix = L"_slot" + std::to_wstring(slot) + L".sav";
    int n = 0;
    for (auto it = fs::directory_iterator(dir, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        const std::wstring name = it->path().filename().wstring();
        if (name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
            std::error_code e2;
            if (fs::remove(it->path(), e2)) ++n;
        }
    }
    if (n) QD_INFO(kTag, "new game in slot {}: {} character(s) kept for its old game removed", slot, n);
}

void CharacterInit() {
    auto& s = S();
    if (s.inited) return;
    s.inited = true;
    coop::On(net::MsgType::Character, OnCharacter);
    s.ticker = gamethread::AddTicker("character", [](float dt) { Tick(dt); });
    QD_INFO(kTag, "initialized (co-op characters in slot {})", kCoopSlot);
}

}  // namespace qd::sync
