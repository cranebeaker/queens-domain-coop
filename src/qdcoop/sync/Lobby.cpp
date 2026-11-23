// Knights' Domain: the waiting room (see Lobby.h).
//
// The main menu's buttons are the game's own: TitleScreenUI_C.LoadGameButton ("Play Game") and LoadSavedGameBUtton.
// While a lobby sits on the menu they are disabled (SetIsEnabled) until the host may start: the host's Play Game is held
// until every client is ready, a client's stays held until the host starts. The host then uses the game's own save
// menu; when it calls "Load Slot" (a save) or "NewGameOnNewSlotConfirmed" (a new game) the host tells everyone, and every
// client runs the same calls on its own menu, for the slot the host picked (see StartHere for a client's own save).
//
// Include order: SDK headers FIRST (see GameQueries.cpp for why).
#include "SDK/Engine_classes.hpp"
#include "SDK/Engine_parameters.hpp"
#include "SDK/UMG_classes.hpp"
#include "SDK/UMG_parameters.hpp"
#include "SDK/CommonUI_classes.hpp"
#include "SDK/FS_Button_classes.hpp"
#include "SDK/TitleScreenUI_classes.hpp"
#include "SDK/TitleScreenUI_parameters.hpp"
#include "SDK/SaveSlotWidget_classes.hpp"
#include "SDK/FS_SaveSlot_classes.hpp"

#include "qdcoop/sync/Lobby.h"

#include "qdcoop/core/Core.h"
#include "qdcoop/core/Hooks.h"
#include "qdcoop/core/Log.h"
#include "qdcoop/core/Unreal.h"
#include "qdcoop/net/Protocol.h"
#include "qdcoop/sync/Coop.h"
#include "qdcoop/ui/I18n.h"
#include "qdcoop/ui/Overlay.h"

#include <mutex>

namespace qd::sync {
namespace {

constexpr const char* kTag = "lobby";
constexpr const char* kTitleClass = "/Game/UI/Title/TitleScreenUI.TitleScreenUI_C";
constexpr const char* kFnLoadSlot = "/Game/UI/Title/TitleScreenUI.TitleScreenUI_C:Load Slot";
constexpr const char* kFnSlotSelected = "/Game/UI/Title/TitleScreenUI.TitleScreenUI_C:SaveSlotSelected";
constexpr const char* kFnNewGame = "/Game/UI/Title/TitleScreenUI.TitleScreenUI_C:NewGameOnNewSlotConfirmed";
constexpr float kCheckSec = 0.25f;
constexpr float kPendingGiveUpSec = 90.0f;
constexpr int kSlotCount = 5;   // SaveSlotWidget_0..4 on the title screen

struct State {
    bool inited = false;
    gamethread::TickerId ticker = 0;
    float checkIn = 0;

    bool onTitle = false, prevOnTitle = false;
    bool started = false;        // the host already started: nobody is held at the menu
    uint8_t readyMask = 0;       // bit per slot (host: authoritative; clients: the host's last State)
    bool localReady = false;
    bool toldWaiting = false;
    bool toldAllReady = false;
    bool gated = false;          // the buttons are disabled by us
    SDK::UObject* gatedTitle = nullptr;

    bool pending = false;        // a Start arrived: follow it as soon as the menu is up
    uint8_t pendingSlot = 0;
    bool pendingNew = false;
    float pendingAge = 0;

    bool doingStart = false;     // our own calls into the menu: the hooks must not tell anyone
    int lastSelected = -1;       // the slot the host last selected on the menu
    bool lastSelectedEmpty = false;   // it had no save when it was picked (a new game)
    bool newGameRunning = false;      // inside NewGameOnNewSlotConfirmed on this machine

    std::mutex viewMx;
    LobbyView view;
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

SDK::UTitleScreenUI_C* FindTitle() {
    SDK::UClass* cls = ue::FindClass(kTitleClass);
    if (!cls) return nullptr;
    auto& objs = SDK::UObject::GObjects;
    for (int i = 0; i < objs->Num(); ++i) {
        SDK::UObject* o = objs->GetByIndex(i);
        if (o && !o->IsDefaultObject() && o->IsA(cls) && ue::IsValid(o)) {
            auto* t = static_cast<SDK::UTitleScreenUI_C*>(o);
            // the menu is built after the intro: until Play Game exists there is nothing to hold
            if (t->LoadGameButton && ue::IsValid(t->LoadGameButton)) return t;
        }
    }
    return nullptr;
}

std::string NameOf(uint8_t slot) {
    for (const auto& m : coop::Members())
        if (m.present && m.slot == slot) return m.name;
    return ui::Tr("name.default") + std::to_string(slot + 1);
}

bool AllClientsReady() {
    for (const auto& m : coop::Members())
        if (m.present && !m.isLocal && !(S().readyMask & (1u << m.slot))) return false;
    return true;
}

bool HasClients() {
    for (const auto& m : coop::Members())
        if (m.present && !m.isLocal) return true;
    return false;
}

void SendState() {
    if (!coop::IsHost() || !coop::InSession()) return;
    net::MsgLobby m;
    m.kind = net::LobbyKind::State;
    m.readyMask = S().readyMask;
    m.started = S().started ? 1 : 0;
    coop::Broadcast(net::Channel::Reliable, net::Encode(m));
}

void SetButtons(SDK::UTitleScreenUI_C* t, bool enabled) {
    SDK::UWidget* buttons[] = {static_cast<SDK::UWidget*>(t->LoadGameButton), static_cast<SDK::UWidget*>(t->LoadSavedGameBUtton)};
    for (SDK::UWidget* b : buttons) {
        if (!b || !ue::IsValid(b)) continue;
        struct { bool enabled; } p{enabled};
        if (SDK::UFunction* f = ue::FindFunction(b, "SetIsEnabled")) ue::CallFunction(b, f, &p);
    }
}

bool SlotHasSave(SDK::UTitleScreenUI_C* t, int slot) {
    for (int i = 0; i < t->SaveSlots.Num(); ++i) {
        SDK::USaveSlotWidget_C* w = t->SaveSlots[i];
        if (!w || !ue::IsValid(w) || w->Index_0 != slot) continue;
        return w->My_Save && ue::IsValid(w->My_Save) && w->My_Save->HasEverSaved;
    }
    return false;
}

// Boots this machine into its own save of `slot` (a new game when it has none, or when the host starts a new game and
// this player's slot is taken: then the first free slot, so nobody's save is overwritten).
void StartHere(SDK::UTitleScreenUI_C* t, int slot, bool isNew) {
    auto& s = S();
    const bool has = SlotHasSave(t, slot);
    int target = slot;
    bool load = !isNew && has;
    if (isNew && has) {
        int free = -1;
        for (int i = 0; i < kSlotCount && free < 0; ++i)
            if (!SlotHasSave(t, i)) free = i;
        if (free >= 0) target = free;
        else load = true;   // no free slot: carry on with this player's own save
    }
    QD_INFO(kTag, "starting on this machine: host picked slot {} ({}), this machine {} slot {}", slot, isNew ? "new game" : "save",
            load ? "loads" : "starts a new game in", target);
    s.doingStart = true;
    if (load) {
        SDK::Params::TitleScreenUI_C_Load_Slot p{};
        p.Slot_0 = target;
        p.Backup = false;
        p.BackupSlot = 0;
        Call(t, "Load Slot", p);
    } else {
        SDK::Params::TitleScreenUI_C_SaveSlotSelected p{};
        p.Index_0 = target;
        p.Backup = false;
        p.BackupSlot = 0;
        Call(t, "SaveSlotSelected", p);
        char none = 0;
        Call(t, "NewGameOnNewSlotConfirmed", none);
    }
    s.doingStart = false;
    s.started = true;
    s.pending = false;
}

// ---- messages

void OnLobby(uint8_t dispatchSlot, const uint8_t* data, size_t size) {
    net::MsgLobby m;
    if (!net::Decode(data, size, m)) return;
    auto& s = S();
    auto local = coop::LocalSlot();
    if (!local) return;
    if (coop::IsHost()) {
        if (m.kind != net::LobbyKind::Ready || dispatchSlot >= net::kMaxPlayers || dispatchSlot == *local) return;
        const uint8_t bit = static_cast<uint8_t>(1u << dispatchSlot);
        const uint8_t before = s.readyMask;
        s.readyMask = (m.readyMask & 1) ? static_cast<uint8_t>(s.readyMask | bit) : static_cast<uint8_t>(s.readyMask & ~bit);
        if (s.readyMask != before) {
            QD_INFO(kTag, "{} is {}", NameOf(dispatchSlot), (m.readyMask & 1) ? "ready" : "not ready");
            SendState();
        }
        return;
    }
    if (dispatchSlot != 0) return;   // only the host speaks for the room
    if (m.kind == net::LobbyKind::State) {
        s.readyMask = m.readyMask;
        s.started = m.started != 0;
        s.localReady = (m.readyMask & (1u << *local)) != 0;
    } else if (m.kind == net::LobbyKind::Start) {
        s.started = true;
        s.pending = true;
        s.pendingSlot = m.saveSlot;
        s.pendingNew = m.isNew != 0;
        s.pendingAge = 0;
        ui::Toast(ui::TrF("lobby.host_started", NameOf(0)), ui::ToastKind::Success, 5.0f);
    }
}

void HostStarts(int slot, bool isNew) {
    auto& s = S();
    if (!coop::IsHost() || !coop::InSession() || s.doingStart || s.started) return;
    s.started = true;
    net::MsgLobby m;
    m.kind = net::LobbyKind::Start;
    m.started = 1;
    m.readyMask = s.readyMask;
    m.saveSlot = static_cast<uint8_t>(slot < 0 || slot >= 16 ? 0 : slot);
    m.isNew = isNew ? 1 : 0;
    coop::Broadcast(net::Channel::Reliable, net::Encode(m));
    QD_INFO(kTag, "host starts: slot {} ({})", slot, isNew ? "new game" : "save");
}

void OnMember(const coop::Member& mem, bool joined) {
    auto& s = S();
    if (!coop::IsHost() || mem.isLocal) return;
    s.readyMask = static_cast<uint8_t>(s.readyMask & ~(1u << mem.slot));   // a newcomer starts not ready
    s.toldAllReady = false;
    SendState();
    if (joined) QD_INFO(kTag, "{} joined the waiting room", mem.name);
}

void PublishView(bool waiting) {
    auto& s = S();
    LobbyView v;
    v.waiting = waiting;
    v.isHost = coop::IsHost();
    v.localReady = s.localReady;
    v.allReady = AllClientsReady();
    for (const auto& m : coop::Members()) {
        if (!m.present) continue;
        LobbyRow r;
        r.slot = m.slot;
        r.name = m.name;
        r.isLocal = m.isLocal;
        r.isHost = m.slot == 0;
        r.ready = r.isHost || (s.readyMask & (1u << m.slot)) != 0;
        v.rows.push_back(std::move(r));
    }
    std::lock_guard lk(s.viewMx);
    s.view = std::move(v);
}

void Tick(float dt) {
    auto& s = S();
    s.checkIn -= dt;
    if (s.pending) s.pendingAge += dt;
    if (s.checkIn > 0) return;
    s.checkIn = kCheckSec;

    SDK::UTitleScreenUI_C* title = FindTitle();
    s.onTitle = title != nullptr;
    const bool session = coop::InSession();
    const bool host = coop::IsHost();

    if (s.onTitle && !s.prevOnTitle) {   // back at (or arriving at) the main menu: a fresh round
        if (!s.pending) {
            s.started = false;
            s.readyMask = 0;
            s.localReady = false;
        }
        s.toldWaiting = false;
        s.toldAllReady = false;
        if (host && session) SendState();
    }
    s.prevOnTitle = s.onTitle;
    if (!s.onTitle && host) s.started = true;   // the host is in the game: a newcomer is not held
    if (!session) {
        s.readyMask = 0;
        s.localReady = false;
        s.toldWaiting = false;
        s.toldAllReady = false;
        if (host) s.started = false;
    }

    // a Start came: follow it as soon as the menu is up
    if (s.pending && title) StartHere(title, s.pendingSlot, s.pendingNew);
    if (s.pending && s.pendingAge > kPendingGiveUpSec) {
        QD_WARN(kTag, "the host's start could not be followed (no main menu after {:.0f} s)", s.pendingAge);
        s.pending = false;
    }

    const bool waiting = s.onTitle && session && !s.started;
    const bool hold = waiting && (!host || !AllClientsReady());
    if (title && (hold || s.gated)) {
        SetButtons(title, !hold);
        s.gated = hold;
        s.gatedTitle = title;
    } else if (!title) {
        s.gated = false;
        s.gatedTitle = nullptr;
    }
    if (waiting && !s.toldWaiting) {
        s.toldWaiting = true;
        ui::Toast(ui::Tr(host ? "lobby.toast_host" : "lobby.toast_client"), ui::ToastKind::Info, 8.0f);
    }
    if (waiting && host && HasClients() && AllClientsReady() && !s.toldAllReady) {
        s.toldAllReady = true;
        ui::Toast(ui::Tr("lobby.all_ready"), ui::ToastKind::Success, 6.0f);
    }
    PublishView(waiting);
}

}  // namespace

LobbyView LobbyViewNow() {
    auto& s = S();
    std::lock_guard lk(s.viewMx);
    return s.view;
}

void LobbySetReady(bool ready) {
    gamethread::Post([ready] {
        auto& s = S();
        if (coop::IsHost() || !coop::InSession() || s.started) return;
        s.localReady = ready;
        net::MsgLobby m;
        m.kind = net::LobbyKind::Ready;
        m.readyMask = ready ? 1 : 0;
        coop::SendToHost(net::Channel::Reliable, net::Encode(m));
    });
}

void LobbyInit() {
    auto& s = S();
    if (s.inited) return;
    s.inited = true;
    coop::On(net::MsgType::Lobby, OnLobby);
    coop::AddMemberListener(OnMember);
    // Before the game handles the pick: was the slot empty? (Picking an empty slot is a new game.)
    hooks::AddPre(kFnSlotSelected, [](hooks::CallInfo& ci) {
        auto& st = S();
        if (st.doingStart || !ci.params) return false;
        st.lastSelected = static_cast<SDK::Params::TitleScreenUI_C_SaveSlotSelected*>(ci.params)->Index_0;
        st.lastSelectedEmpty = ci.object && !SlotHasSave(static_cast<SDK::UTitleScreenUI_C*>(ci.object), st.lastSelected);
        return false;
    });
    // A new game loads its fresh slot through "Load Slot" from inside NewGameOnNewSlotConfirmed, so that load must be
    // told to everyone as a new game, not as "load your save of this slot".
    hooks::AddPre(kFnNewGame, [](hooks::CallInfo&) {
        auto& st = S();
        if (!st.doingStart) st.newGameRunning = true;
        return false;
    });
    hooks::AddPost(kFnLoadSlot, [](hooks::CallInfo& ci) {
        auto& st = S();
        if (st.doingStart || !ci.params) return;
        const int slot = static_cast<SDK::Params::TitleScreenUI_C_Load_Slot*>(ci.params)->Slot_0;
        const bool isNew = st.newGameRunning || (slot == st.lastSelected && st.lastSelectedEmpty);
        HostStarts(slot, isNew);
    });
    hooks::AddPost(kFnNewGame, [](hooks::CallInfo&) {
        auto& st = S();
        st.newGameRunning = false;
        if (st.doingStart) return;
        HostStarts(st.lastSelected, true);
    });
    s.ticker = gamethread::AddTicker("lobby", [](float dt) { Tick(dt); });
    QD_INFO(kTag, "initialized");
}

void LobbyShutdown() {
    auto& s = S();
    if (s.ticker) {
        gamethread::RemoveTicker(s.ticker);
        s.ticker = 0;
    }
}

#if QD_DEV
void LobbyDebug(const std::string& args) {
    auto& s = S();
    if (args.rfind("ready", 0) == 0) {
        LobbySetReady(args.find('1') != std::string::npos);
        return;
    }
    if (args.rfind("start", 0) == 0) {
        int slot = 0, isNew = 1;
        sscanf_s(args.c_str(), "start %d %d", &slot, &isNew);
        if (SDK::UTitleScreenUI_C* t = FindTitle()) StartHere(t, slot, isNew != 0);
        else QD_WARN(kTag, "lobby start: no main menu");
        return;
    }
    const LobbyView v = LobbyViewNow();
    std::string rows;
    for (const auto& r : v.rows)
        rows += std::string(rows.empty() ? "" : ",") + "{\"slot\":" + std::to_string(r.slot) + ",\"ready\":" + (r.ready ? "true" : "false") + "}";
    SDK::UTitleScreenUI_C* t = FindTitle();
    bool enabled = true;
    if (t && t->LoadGameButton) {
        struct { bool v; } p{true};
        if (SDK::UFunction* f = ue::FindFunction(t->LoadGameButton, "GetIsEnabled")) {
            ue::CallFunction(t->LoadGameButton, f, &p);
            enabled = p.v;
        }
    }
    QD_INFO(kTag, "lobby state {{\"onTitle\":{},\"waiting\":{},\"started\":{},\"host\":{},\"allReady\":{},\"localReady\":{},\"playEnabled\":{},\"rows\":[{}]}}",
            s.onTitle, v.waiting, s.started, v.isHost, v.allReady, v.localReady, enabled, rows);
}
#endif

}  // namespace qd::sync
