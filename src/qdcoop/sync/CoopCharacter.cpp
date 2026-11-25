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
#include "SDK/FS_PlayerChar_classes.hpp"
#include "SDK/FS_PlayerChar_parameters.hpp"
#include "SDK/FS_SaveSlot_classes.hpp"

#include "qdcoop/sync/CoopCharacter.h"

#include "qdcoop/core/Config.h"
#include "qdcoop/core/Core.h"
#include "qdcoop/core/Log.h"
#include "qdcoop/core/Unreal.h"
#include "qdcoop/net/Protocol.h"
#include "qdcoop/sync/Coop.h"
#include "qdcoop/sync/CoopInternal.h"
#include "qdcoop/sync/GameQueries.h"
#include "qdcoop/sync/PlayerSync.h"
#include "qdcoop/sync/Summon.h"
#include "qdcoop/ui/I18n.h"
#include "qdcoop/ui/Overlay.h"

#include <Windows.h>

#include <chrono>
#include <ctime>
#include <cmath>
#include <algorithm>
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
constexpr double kReplyTimeoutSec = 20.0;
constexpr double kSaveEverySec = 120.0;      // the co-op character is written this often while playing
constexpr double kTeleportStuckSec = 25.0;   // a teleport that has not ended by then is ended the game's own way
constexpr int    kHostHistory = 5;           // older copies the host keeps of each character

using Clock = std::chrono::steady_clock;
double Now() { return std::chrono::duration<double>(Clock::now().time_since_epoch()).count(); }

struct Incoming {   // one file arriving in chunks
    uint32_t id = 0, total = 0, version = 0;
    std::vector<uint8_t> data;
    bool active = false;
};

enum class Boot { None, Asked, Ready, Loading };
// After a client joins a running game: into the world, past the intro, to the host's last checkpoint the game's own way
// (so that area is loaded: a plain move to the host's spot fell through the unloaded ground), then next to the host.
enum class Place { None, Settle, AskWhere, Travel, Arrive, Done };

struct State {
    bool inited = false;
    gamethread::TickerId ticker = 0;

    // client
    Boot boot = Boot::None;
    bool togetherFresh = false;
    double askedAt = 0;
    bool haveData = false;
    int32_t localVersion = 0;   // this machine's own copy, when asked
    std::vector<uint8_t> data;   // the character from the host (empty: a fresh one)
    Incoming fromHost;
    bool playingCoop = false;    // the loaded game is the co-op character
    double inGameSince = -1;
    Place place = Place::None;
    double placeAt = 0;
    std::string hostWhere;      // the host's last checkpoint
    bool gotWhere = false;
    double nextSave = 0;
    bool wasInSession = false;
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

// ---- versions: a co-op character's play time in seconds (it only grows), so a newer copy never loses to an older one

int32_t VersionOf(SDK::USaveGame* g) {
    if (!g || !ue::IsValid(g) || !g->IsA(SDK::UFS_SaveSlot_C::StaticClass())) return 0;
    auto* s = static_cast<SDK::UFS_SaveSlot_C*>(g);
    return std::max(s->PlayTime_Seconds, s->GamePlayTime_Seconds);
}

// The version of this machine's own copy of the co-op character (the newest of its main and suspend files).
int32_t LocalVersion() {
    int32_t best = 0;
    std::vector<std::wstring> names;
    if (std::wstring n = SlotName(kCoopSlot, false, 0); !n.empty()) names.push_back(n);
    for (int i = 0; i < kSuspendSlots; ++i)
        if (std::wstring n = SlotName(kCoopSlot, true, i); !n.empty()) names.push_back(n);
    for (const auto& n : names) {
        if (!SDK::UGameplayStatics::DoesSaveGameExist(SDK::FString(n.c_str()), 0)) continue;
        best = std::max(best, VersionOf(SDK::UGameplayStatics::LoadGameFromSlot(SDK::FString(n.c_str()), 0)));
    }
    return best;
}

int32_t ActiveVersion(SDK::UObject* ctx) {
    SDK::UObject* cdo = SaveFunctions();
    if (!cdo) return 0;
    SDK::Params::SaveFunctions_C_GetActiveSave p{};
    p.__WorldContext = ctx;
    if (!Call(cdo, "GetActiveSave", p)) return 0;
    return VersionOf(p.ActiveSave);
}

// Moves this machine's co-op files aside (never deleted): ModSaved\characters\backup\<time>\.
void BackUpLocal(const char* why) {
    std::error_code ec;
    const fs::path dir = fs::path(paths::ModSaved()) / L"characters" / L"backup" / std::to_wstring(static_cast<long long>(std::time(nullptr)));
    int n = 0;
    for (const auto& f : CoopFiles()) {
        if (!fs::is_regular_file(f, ec)) continue;
        fs::create_directories(dir, ec);
        fs::rename(f, dir / f.filename(), ec);
        if (ec) fs::copy_file(f, dir / f.filename(), fs::copy_options::overwrite_existing, ec);
        ++n;
    }
    if (n) QD_INFO(kTag, "{} co-op file(s) moved to {} ({})", n, dir.string(), why);
}

// The game's own save of the active save (what a checkpoint writes), here: the co-op character's file.
void SaveNow(SDK::UObject* ctx, const char* why) {
    SDK::UObject* cdo = SaveFunctions();
    if (!cdo || !ctx) return;
    SDK::Params::SaveFunctions_C_SaveGameToDisk p{};
    p.__WorldContext = ctx;
    if (Call(cdo, "SaveGameToDisk", p)) QD_INFO(kTag, "co-op character saved ({})", why);
}

fs::path HostVersionFile(const fs::path& store) {
    fs::path v = store;
    v.replace_extension(L".ver");
    return v;
}

uint32_t HostVersion(const fs::path& store) {
    std::ifstream f(HostVersionFile(store));
    uint32_t v = 0;
    f >> v;
    return v;
}

// ---- sending a file in chunks

void SendFile(uint8_t toSlot, uint8_t purpose, const std::vector<uint8_t>& bytes, uint32_t version) {
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
    m.offset = version;
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
            in.version = m.offset;
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
    // Nothing is ever deleted: what is replaced moves to ModSaved\characters\backup.
    if (s.togetherFresh) BackUpLocal("a new game started together: a fresh character");
    if (s.haveData && !s.data.empty()) {
        BackUpLocal("the host has a newer copy");
        const std::wstring name = SlotName(kCoopSlot, false, 0);
        const fs::path file = SaveDir() / (name + L".sav");
        if (name.empty() || !WriteFileAtomic(file, s.data)) {
            QD_WARN(kTag, "could not write the co-op character to {}; starting a fresh one", file.string());
        } else {
            QD_INFO(kTag, "co-op character from the host written to {} ({} bytes)", file.string(), s.data.size());
        }
    } else if (!s.togetherFresh) {
        QD_INFO(kTag, "loading this machine's own copy of the co-op character (version {})", s.localVersion);
    }
    if (SDK::UFS_SystemSave_C* sys = SystemSave(title)) s.savedSelectedSlot = sys->SelectedSaveSlot;
    SDK::Params::TitleScreenUI_C_Load_Slot p{};
    p.Slot_0 = kCoopSlot;
    p.Backup = false;
    p.BackupSlot = 0;
    s.boot = Boot::Loading;
    s.playingCoop = true;
    s.inGameSince = -1;
    s.place = s.togetherFresh ? Place::None : Place::Settle;
    s.placeAt = 0;
    s.gotWhere = false;
    s.nextSave = Now() + kSaveEverySec;
    s.lastSentTime = {};
    s.nextUploadCheck = Now() + kUploadCheckSec;
    Call(title, "Load Slot", p);
    ui::Toast(ui::Tr(s.haveData || s.localVersion > 0 ? "char.loading" : "char.fresh"), ui::ToastKind::Info, 5.0f);
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
    const uint32_t version = static_cast<uint32_t>(std::max(0, ActiveVersion(ue::LocalCharacter())));
    SendFile(0, 1, bytes, version);
    QD_INFO(kTag, "co-op character sent to the host ({} bytes, version {}, from {})", bytes.size(), version, newest.filename().string());
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
        if (m.kind == net::CharacterKind::Where) {
            net::MsgCharacter here;
            here.kind = net::CharacterKind::Here;
            std::string where;
            SDK::UObject* cdo = SaveFunctions();
            SDK::Params::SaveFunctions_C_GetActiveSave gs{};
            gs.__WorldContext = ue::LocalCharacter();
            if (cdo && gs.__WorldContext && Call(cdo, "GetActiveSave", gs) && gs.ActiveSave) {
                SDK::UObject* loc = gs.ActiveSave->LastTouchedCheckpoint ? static_cast<SDK::UObject*>(gs.ActiveSave->LastTouchedCheckpoint)
                                                                         : static_cast<SDK::UObject*>(gs.ActiveSave->SavePointID);
                if (loc && ue::IsValid(loc)) where = ue::PathName(loc);
            }
            here.data.assign(where.begin(), where.end());
            if (here.data.size() > 1024) here.data.clear();
            coop::SendToSlot(fromSlot, net::Channel::Reliable, net::Encode(here));
            QD_INFO(kTag, "told slot {} where the host is: {}", fromSlot, where.empty() ? std::string("(unknown)") : where);
            return;
        }
        if (m.kind == net::CharacterKind::Request) {
            const fs::path p = HostStore(*peer, hostSlot);
            const uint32_t hostVer = HostVersion(p), clientVer = m.total;
            std::vector<uint8_t> bytes;
            std::error_code ec;
            if (fs::is_regular_file(p, ec) && hostVer > clientVer && ReadFile(p, bytes) && !bytes.empty()) {
                SendFile(fromSlot, 0, bytes, hostVer);
                QD_INFO(kTag, "sent slot {}'s character (version {}, theirs {}; host slot {})", fromSlot, hostVer, clientVer, hostSlot);
            } else {
                net::MsgCharacter none;
                none.kind = net::CharacterKind::None;
                coop::SendToSlot(fromSlot, net::Channel::Reliable, net::Encode(none));
                QD_INFO(kTag, "slot {} keeps its own character (theirs version {}, kept here {})", fromSlot, clientVer, hostVer);
            }
            return;
        }
        if (m.purpose != 1) return;
        Incoming& in = s.uploads[fromSlot];
        if (!Collect(in, m)) return;
        if (!ue::LocalCharacter()) return;   // the host is not in a game: nothing to keep it for
        const fs::path p = HostStore(*peer, hostSlot);
        const uint32_t kept = HostVersion(p);
        std::error_code ec2;
        if (in.version < kept && fs::is_regular_file(p, ec2)) {
            QD_WARN(kTag, "slot {} sent an older character (version {}, kept {}): the kept one stays", fromSlot, in.version, kept);
            in = Incoming{};
            return;
        }
        if (fs::is_regular_file(p, ec2)) {   // the copy it replaces goes to the history (the last few stay)
            const fs::path hist = p.parent_path() / L"history";
            fs::create_directories(hist, ec2);
            fs::copy_file(p, hist / (p.stem().wstring() + L"_v" + std::to_wstring(kept) + L".sav"), fs::copy_options::overwrite_existing, ec2);
            std::vector<fs::path> mine;
            for (auto it = fs::directory_iterator(hist, ec2); !ec2 && it != fs::directory_iterator(); it.increment(ec2))
                if (it->path().filename().wstring().rfind(p.stem().wstring() + L"_v", 0) == 0) mine.push_back(it->path());
            std::sort(mine.begin(), mine.end(), [](const fs::path& a, const fs::path& b) { return fs::last_write_time(a) < fs::last_write_time(b); });
            while (mine.size() > static_cast<size_t>(kHostHistory)) {
                fs::remove(mine.front(), ec2);
                mine.erase(mine.begin());
            }
        }
        if (WriteFileAtomic(p, in.data)) {
            std::ofstream(HostVersionFile(p), std::ios::trunc) << in.version << "\n";
            QD_INFO(kTag, "kept slot {}'s character ({} bytes, version {}) for host slot {}", fromSlot, in.data.size(), in.version, hostSlot);
        } else {
            QD_WARN(kTag, "could not keep slot {}'s character in {}", fromSlot, p.string());
        }
        in = Incoming{};
        return;
    }
    if (fromSlot == 0 && m.kind == net::CharacterKind::Here) {
        s.hostWhere.assign(m.data.begin(), m.data.end());
        s.gotWhere = true;
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

void EndTeleport(SDK::AFS_PlayerChar_C* p) {
    char none = 0;
    Call(p, "FinishTeleport", none);
    Call(p, "CleanUpTeleportProcess", none);
    Call(p, "UnlockInputForLoadingOrCutscene", none);
    Call(p, "SlightDelayEnableInput", none);
}

void TickPlace(SDK::AFS_PlayerChar_C* p, double now) {
    auto& s = S();
    auto next = [&](Place pl) {
        s.place = pl;
        s.placeAt = now;
    };
    if (s.placeAt <= 0) s.placeAt = now;
    const double in = now - s.placeAt;
    switch (s.place) {
        case Place::None:
        case Place::Done:
            return;
        case Place::Settle:   // into the world; the intro of a new character is not for someone joining a running game
            if (p->InIntroCutscene) {
                char none = 0;
                Call(p, "SkipIntroCutscene", none);
                QD_INFO(kTag, "joining a running game: intro skipped");
            }
            if (in < 2.0) return;
            {
                net::MsgCharacter m;
                m.kind = net::CharacterKind::Where;
                coop::SendToHost(net::Channel::Reliable, net::Encode(m));
            }
            ui::Toast(ui::Tr("char.placing"), ui::ToastKind::Info, 4.0f);
            next(Place::AskWhere);
            return;
        case Place::AskWhere: {
            if (!s.gotWhere && in < 8.0) return;
            // already close to the host (the same area loaded): straight next to them
            net::Vec3f feet{};
            float yaw = 0;
            const bool closeBy = playersync::RemotePose(0, feet, yaw) &&
                              std::hypot(p->K2_GetActorLocation().X - feet.x, p->K2_GetActorLocation().Y - feet.y) < 3000.0f;
            SDK::UObject* loc = !closeBy && !s.hostWhere.empty() ? ue::LoadObject(s.hostWhere) : nullptr;
            if (loc) {
                SDK::Params::FS_PlayerChar_C_TeleportToLocation t{};
                t.Location_Data = static_cast<SDK::UFS_LocationData*>(loc);
                t.GreenFX = false;
                if (Call(p, "TeleportToLocation", t)) {
                    QD_INFO(kTag, "joining: travelling to the host's checkpoint {}", s.hostWhere);
                    next(Place::Travel);
                    return;
                }
            }
            next(Place::Arrive);
            return;
        }
        case Place::Travel:
            if (p->Teleporting || p->QuickTeleporting) {
                if (in > kTeleportStuckSec) {
                    QD_WARN(kTag, "joining: the travel did not end in {:.0f} s, ending it the game's own way", in);
                    EndTeleport(p);
                    next(Place::Arrive);
                }
                return;
            }
            if (in < 2.0) return;
            next(Place::Arrive);
            return;
        case Place::Arrive:
            if (in < 1.5) return;
            SummonToHost();   // next to the host (it holds the player until the place has loaded)
            next(Place::Done);
            return;
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
        QD_WARN(kTag, "the host did not answer with a character in {:.0f} s; using this machine's own copy", kReplyTimeoutSec);
        s.haveData = false;
        s.data.clear();
        s.boot = Boot::Ready;
    }
    if (s.boot == Boot::Ready && title) LoadCoopSlot(title);
    if (s.boot == Boot::Loading && ch) s.boot = Boot::None;

    // leaving (or losing) the session while playing the co-op character: it is saved here at once
    if (s.wasInSession && !session && !host && ch && s.playingCoop) SaveNow(ch, "left the session");
    s.wasInSession = session && !host;
    if (!session || host || !ch || !s.playingCoop) {
        s.inGameSince = -1;
        return;
    }
    if (s.inGameSince < 0) s.inGameSince = now;
    auto* p = static_cast<SDK::AFS_PlayerChar_C*>(ch);
    TickPlace(p, now);
    // regularly: the game's own save of the co-op character (then sent to the host by the upload check)
    if (now >= s.nextSave && s.place == Place::Done || (now >= s.nextSave && s.place == Place::None)) {
        s.nextSave = now + kSaveEverySec;
        if (!p->IsDead && !p->Teleporting && !p->QuickTeleporting && !p->InIntroCutscene && !p->CurrentCutsceneManager)
            SaveNow(ch, "every two minutes");
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
    s.localVersion = togetherFresh ? 0 : LocalVersion();
    net::MsgCharacter m;
    m.kind = net::CharacterKind::Request;
    m.total = static_cast<uint32_t>(std::max(0, s.localVersion));
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
