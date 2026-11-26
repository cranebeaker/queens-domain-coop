// Knights' Domain: party features. See Party.h.
//
// Down, not dead: the game kills the player through FS_PlayerChar_C:Kill. While a teammate in the same area is up, that
// call is skipped and the player is down instead: no movement, attacks, items or interaction, no damage taken, and
// everyone sees them lying. A teammate who holds Interact next to them for kReviveSec revives them at kReviveFraction of
// their health. Nobody left who could revive them (all down, gone, in another area) -> the game's own death runs.
//
// Party rest / party save: a player who presses Rest or Save in a checkpoint's menu tells the others; each of them is
// taken there with the game's own fast travel to that checkpoint, opens the checkpoint's menu and presses the same
// button through the menu's own handler, then leaves through the menu's own Leave, so HUD, controls and enemies come
// back the way the game does it. Someone dead, in a cutscene or already resting is skipped and told.
//
// Trading: two players within kTradeRangeCm open a trade (request, accept), each offers consumables from their own
// inventory, both confirm the current offers, the requester commits, the other hands over and answers Done, then the
// requester hands over. An offer that changes takes both confirmations back.
// Include order: SDK headers FIRST (see GameQueries.cpp for why).
#include "SDK/Engine_classes.hpp"
#include "SDK/QueensDomain_classes.hpp"
#include "SDK/QueensDomain_parameters.hpp"
#include "SDK/FB_Utils_classes.hpp"
#include "SDK/FB_Utils_parameters.hpp"
#include "SDK/UMG_parameters.hpp"
#include "SDK/FS_PlayerChar_classes.hpp"
#include "SDK/FS_PlayerChar_parameters.hpp"
#include "SDK/Checkpoint_classes.hpp"
#include "SDK/Checkpoint_parameters.hpp"
#include "SDK/SaveMenu_classes.hpp"
#include "SDK/SaveMenu_parameters.hpp"
#include "SDK/SaveFunctions_classes.hpp"
#include "SDK/SaveFunctions_parameters.hpp"
#include "SDK/FS_SaveSlot_classes.hpp"
#include "SDK/FS_ItemData_classes.hpp"
#include "SDK/TimeDilationManagement_parameters.hpp"

#include "qdcoop/sync/Party.h"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <set>

#include "qdcoop/core/Core.h"
#include "qdcoop/core/Hooks.h"
#include "qdcoop/core/Log.h"
#include "qdcoop/core/Unreal.h"
#include "qdcoop/sync/Coop.h"
#include "qdcoop/sync/EnemySync.h"
#include "qdcoop/sync/PlayerSync.h"
#include "qdcoop/core/Config.h"
#include "qdcoop/core/Util.h"
#include "qdcoop/sync/CoopInternal.h"
#include "qdcoop/sync/GameQueries.h"
#include "qdcoop/sync/BossSync.h"
#include "qdcoop/sync/WorldSync.h"
#include "qdcoop/sync/CoopCharacter.h"
#include "qdcoop/ui/I18n.h"
#include "qdcoop/ui/Overlay.h"

#include "imgui.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <mutex>
#include <random>
#include <sstream>
#include <format>
#include <string>
#include <vector>

namespace qd::sync {
namespace {

constexpr const char* kTag = "party";

constexpr const char* kPlayerClass = "/Game/Player/FS_PlayerChar.FS_PlayerChar_C";
constexpr const char* kFnKill = "/Game/Player/FS_PlayerChar.FS_PlayerChar_C:Kill";
constexpr const char* kFnPlayerDamage = "/Game/Player/FS_PlayerChar.FS_PlayerChar_C:TryApplyDamage";
constexpr const char* kCheckpointClass = "/Game/LD45/Blueprints/Checkpoint/Checkpoint.Checkpoint_C";
constexpr const char* kSaveMenuClass = "/Game/UI/Menus/SaveMenu.SaveMenu_C";
constexpr const char* kSaveFunctionsClass = "/Game/Blueprints/SaveSystem/SaveFunctions.SaveFunctions_C";
constexpr const char* kFnRestButton =
    "/Game/UI/Menus/SaveMenu.SaveMenu_C:BndEvt__SaveMenu_RestButton_K2Node_ComponentBoundEvent_0_CommonButtonBaseClicked__DelegateSignature";
constexpr const char* kFnSaveButton =
    "/Game/UI/Menus/SaveMenu.SaveMenu_C:BndEvt__SavedPopup_SaveButton_K2Node_ComponentBoundEvent_3_CommonButtonBaseClicked__DelegateSignature";
constexpr const char* kRestButtonName =
    "BndEvt__SaveMenu_RestButton_K2Node_ComponentBoundEvent_0_CommonButtonBaseClicked__DelegateSignature";
constexpr const char* kSaveButtonName =
    "BndEvt__SavedPopup_SaveButton_K2Node_ComponentBoundEvent_3_CommonButtonBaseClicked__DelegateSignature";

// The player's own controls that a downed player loses (look and the pause / status menus stay).
constexpr const char* kBlockedWhileDown[] = {
    "AddMovementInputAxes",
    "InpActEvt_Jump_K2Node_InputActionEvent_0", "InpActEvt_Jump_K2Node_InputActionEvent_1",
    "InpActEvt_Attack_K2Node_InputActionEvent_4", "InpActEvt_Attack_K2Node_InputActionEvent_5",
    "InpActEvt_NextDagger_K2Node_InputActionEvent_6", "InpActEvt_LastDagger_K2Node_InputActionEvent_7",
    "InpActEvt_NormalDaggerThrow_K2Node_InputActionEvent_8", "InpActEvt_UltraAttack_K2Node_InputActionEvent_9",
    "InpActEvt_NextWeapon_K2Node_InputActionEvent_10", "InpActEvt_LastWeapon_K2Node_InputActionEvent_11",
    "InpActEvt_SecondaryAttack_K2Node_InputActionEvent_12", "InpActEvt_SecondaryAttack_K2Node_InputActionEvent_13",
    "InpActEvt_WindDash_K2Node_InputActionEvent_14",
    "InpActEvt_EquipThrowWeapon1_K2Node_InputActionEvent_15", "InpActEvt_EquipThrowWeapon2_K2Node_InputActionEvent_16",
    "InpActEvt_EquipThrowWeapon3_K2Node_InputActionEvent_17", "InpActEvt_EquipWeapon1_K2Node_InputActionEvent_18",
    "InpActEvt_EquipWeapon2_K2Node_InputActionEvent_19", "InpActEvt_EquipWeapon3_K2Node_InputActionEvent_20",
    "InpActEvt_UseItem_K2Node_InputActionEvent_21",
    "InpActEvt_LastItem_K2Node_InputActionEvent_28", "InpActEvt_NextItem_K2Node_InputActionEvent_29",
    "InpActEvt_Interact_K2Node_InputActionEvent_30", "InpActEvt_Interact_K2Node_InputActionEvent_31",
};

constexpr float  kReviveRangeCm     = 250.0f;
constexpr float  kReviveZRangeCm    = 200.0f;
constexpr double kReviveSec         = 3.0;
constexpr float  kReviveFraction    = 0.35f;
constexpr double kReviveImmuneSec   = 2.0;    // no damage right after standing up
constexpr double kBleedOutGraceSec  = 4.0;
constexpr double kGiveUpSec         = 3.0;    // a downed player holds Interact this long to give up    // nobody could revive us this long -> the game's own death
constexpr float  kTradeRangeCm      = 750.0f;
constexpr float  kTradeBreakCm      = 1000.0f;
constexpr float  kRestNearCm        = 1500.0f; // a follower this close to the checkpoint walks over instead of travelling
constexpr double kRestTravelMaxSec  = 40.0;
constexpr double kRestMenuMaxSec    = 6.0;
constexpr double kRestRunMaxSec     = 25.0;

using Clock = std::chrono::steady_clock;
double Now() { return std::chrono::duration<double>(Clock::now().time_since_epoch()).count(); }

float Dist(const SDK::FVector& a, const SDK::FVector& b) {
    const float dx = a.X - b.X, dy = a.Y - b.Y, dz = a.Z - b.Z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}
SDK::FVector V(const net::Vec3f& v) { return SDK::FVector(v.x, v.y, v.z); }
net::Vec3f V(const SDK::FVector& v) { return {v.X, v.Y, v.Z}; }

// ------------------------------------------------------------------ state

enum class TradeStage { None, Outgoing, Incoming, Open };
struct OfferLine { std::string path, name; int count = 0; };
struct InvLine { std::string path, name; int have = 0; };

struct Trade {
    TradeStage stage = TradeStage::None;
    uint32_t   id = 0;
    uint8_t    peer = 0xFF;
    bool       requester = false;
    std::map<std::string, int> mine;     // path -> count offered
    std::vector<OfferLine>     theirs;
    int        myVersion = 0, theirVersion = 0;
    bool       myConfirm = false, theirConfirm = false;
    bool       committing = false;       // requester: commit sent, waiting for Done
    double     openedAt = 0;
};

enum class RestStep { Idle, Travel, FindCheckpoint, OpenMenu, Press, WaitDone, Close };
struct RestJob {
    RestStep step = RestStep::Idle;
    bool     save = false;               // party save (no rest)
    uint8_t  from = 0;
    std::string locPath;
    SDK::FVector pos;
    double   stepAt = 0;
    bool     sawResting = false;
    SDK::ACheckpoint_C* checkpoint = nullptr;
};

struct UiTeammate { uint8_t slot = 0; std::string name; bool downed = false; bool near = false; };
struct UiSnap {
    bool inSession = false, inGame = false, localDowned = false;
    std::vector<UiTeammate> mates;
    Trade trade;
    std::string peerName;
    std::vector<InvLine> inv;
    std::string prompt;          // centre of the screen: what Interact does now / what is happening to us
    float       progress = -1;   // 0..1 under the prompt; -1 = no bar
};

struct State {
    bool inited = false;
    gamethread::TickerId ticker = 0;
    std::vector<hooks::HookId> hooks;
    SDK::UClass* playerCls = nullptr;

    // dev switches
    bool   downEnabled = true;
    bool   forceHold = false;

    // revive
    bool   localDowned = false;
    double downedAt = 0;
    const void* downedChar = nullptr;    // the character that went down (1.6.7: a new one after a load is not down)
    double noReviverSince = -1;
    bool   allowKill = false;
    double immuneUntil = 0;
    bool   downed[net::kMaxPlayers]{};
    int8_t revivingMe = -1;              // slot that is reviving us
    int    reviveTarget = -1;            // slot we are reviving
    double reviveStart = 0;
    float  reviveHpAtStart = 0;
    float  reviveProgress = 0;           // 0..1, for the name plate
    int    reviveCandidate = -1;         // nearest downed teammate in reach (name plate prompt)
    // 1.6.9: a downed player can give up (hold Interact) instead of waiting for a teammate to come over: they stay
    // down until a teammate rests or saves at a checkpoint, which stands them up there (StartRest)
    bool   givenUp = false;
    double giveUpHoldSince = -1;
    float  giveUpProgress = 0;

    // rest
    uint32_t restSeq = 0;
    bool     driving = false;            // we are pressing a checkpoint menu for a party rest: not a rest of our own
    RestJob  rest;

    // trade
    Trade  trade;
    std::vector<InvLine> inv;
    double invAt = 0;

    std::mutex uiMu;
    UiSnap ui;
};
State& S() {
    static State s;
    return s;
}
void NoteBigMoment();   // autosave, below

bool IsFsPlayer(const SDK::UObject* o) {
    auto& s = S();
    if (!ue::IsValid(s.playerCls)) s.playerCls = ue::FindClass(kPlayerClass);
    return o && s.playerCls && o->IsA(s.playerCls);
}
SDK::AFS_PlayerChar_C* LocalPlayer() {
    SDK::ACharacter* ch = ue::LocalCharacter();
    return ch && ue::IsValid(ch) && IsFsPlayer(ch) ? static_cast<SDK::AFS_PlayerChar_C*>(ch) : nullptr;
}

template <class P>
bool Call(SDK::UObject* obj, const char* fn, P& params) {
    SDK::UFunction* f = obj ? ue::FindFunction(obj, fn) : nullptr;
    if (!f) {
        QD_WARN(kTag, "function {} not found on {}", fn, ue::ClassName(obj));
        return false;
    }
    ue::CallFunction(obj, f, &params);
    return true;
}
bool CallNoParams(SDK::UObject* obj, const char* fn) {
    SDK::UFunction* f = obj ? ue::FindFunction(obj, fn) : nullptr;
    if (!f) return false;
    ue::CallFunction(obj, f, nullptr);
    return true;
}

std::string NameOf(uint8_t slot) {
    const auto& m = coop::Members();
    if (slot < m.size() && m[slot].present && !m[slot].name.empty()) return m[slot].name;
    return ui::Tr("name.default") + std::to_string(slot + 1);
}

// ------------------------------------------------------------------ messages

std::string UiNameOf(uint8_t slot);

void Send(net::MsgParty m) {
    auto slot = coop::LocalSlot();
    if (!slot || !coop::InSession()) return;
    m.fromSlot = *slot;
    const auto bytes = net::Encode(m);
    if (!coop::IsHost()) {
        coop::Broadcast(net::Channel::Reliable, bytes);   // to the host, who passes it on
        return;
    }
    if (m.toSlot == net::kPartyAll) coop::Broadcast(net::Channel::Reliable, bytes);
    else if (m.toSlot != *slot) coop::SendToSlot(m.toSlot, net::Channel::Reliable, bytes);
}
void SendTo(uint8_t to, net::PartyKind kind, uint32_t id = 0, int32_t arg = 0, int32_t a = 0) {
    net::MsgParty m;
    m.kind = kind;
    m.toSlot = to;
    m.id = id;
    m.arg = arg;
    m.a = a;
    Send(std::move(m));
}

// ------------------------------------------------------------------ inventory

SDK::UFS_SaveSlot_C* ActiveSave() {
    SDK::UWorld* w = ue::World();
    if (!w || !ue::IsValid(w->OwningGameInstance)) return nullptr;
    SDK::UGameInstance* gi = w->OwningGameInstance;
    if (!gi->IsA(SDK::UFS_GameInstanceBase::StaticClass())) return nullptr;
    auto* base = static_cast<SDK::UFS_GameInstanceBase*>(gi);
    if (base->bLoadingMap) return nullptr;
    SDK::UFS_SaveGame* save = base->BaseActiveSave;
    SDK::UClass* slotCls = SDK::UFS_SaveSlot_C::StaticClass();
    if (!save || !ue::IsValid(save) || !slotCls || !save->IsA(slotCls)) return nullptr;
    return static_cast<SDK::UFS_SaveSlot_C*>(save);
}

// Tradeable: consumables (herbs, potions, throwables...), not weapons, equipment, keys or quest items.
bool Tradeable(const SDK::UFS_ItemData_C* it) {
    if (it && ue::PathName(it) == "/Game/Items/Treasure/Coins/Coin.Coin") return true;   // coins can be traded too
    return it && it->Consumable && !it->Weapon && !it->Equippable && !it->ShowInKeyMenu && !it->Unstackable;
}

std::vector<InvLine> ReadInventory() {
    std::vector<InvLine> out;
    SDK::UFS_SaveSlot_C* save = ActiveSave();
    if (!save) return out;
    SDK::UClass* dataCls = SDK::UFS_ItemData_C::StaticClass();
    for (auto& kv : save->InventoryContents) {   // the live inventory (the Blueprint "Inventory" map is a legacy one)
        SDK::UFS_Item* raw = kv.Key();
        const int n = kv.Value();
        if (!raw || n <= 0 || !ue::IsValid(raw) || !dataCls || !raw->IsA(dataCls)) continue;
        auto* it = static_cast<SDK::UFS_ItemData_C*>(raw);
        if (!Tradeable(it)) continue;
        InvLine l;
        l.path = ue::PathName(it);
        try {
            l.name = it->Name_0.TextData ? it->Name_0.ToString() : ue::Name(it);
        } catch (...) {
            l.name = ue::Name(it);
        }
        if (l.name.empty()) l.name = ue::Name(it);
        l.have = n;
        out.push_back(std::move(l));
    }
    std::sort(out.begin(), out.end(), [](const InvLine& a, const InvLine& b) { return a.name < b.name; });
    return out;
}
int CountOf(const std::string& path) {
    SDK::UFS_SaveSlot_C* save = ActiveSave();
    if (!save) return 0;
    for (auto& kv : save->InventoryContents)
        if (kv.Key() && ue::PathName(kv.Key()) == path) return kv.Value();
    return 0;
}

SDK::UObject* SaveFunctions() {
    SDK::UClass* lib = ue::FindClass(kSaveFunctionsClass);
    return lib ? lib->ClassDefaultObject : nullptr;
}
template <class P>
void SetSoft(P& p, const std::string& path) {
    p.Item.WeakPtr.ObjectIndex = 0;
    p.Item.WeakPtr.ObjectSerialNumber = 0;
    p.Item.TagAtLastTest = -1;
    p.Item.ObjectID.AssetPathName = ue::MakeName(path);   // "Package.Object"
}
bool g_giving = false;   // our own AddItemToInventory calls (trades, shared items): never shared again

bool GiveItem(const std::string& path, int count) {
    SDK::ACharacter* ch = ue::LocalCharacter();
    SDK::UObject* cdo = SaveFunctions();
    if (!ch || !cdo || count <= 0 || !ue::LoadObject(path)) return false;
    SDK::Params::SaveFunctions_C_AddItemToInventory p{};
    SetSoft(p, path);
    p.Count = count;
    p.__WorldContext = ch;
    g_giving = true;
    const bool ok = Call(cdo, "AddItemToInventory", p);
    g_giving = false;
    return ok;
}
bool TakeItem(const std::string& path, int count) {
    SDK::ACharacter* ch = ue::LocalCharacter();
    SDK::UObject* cdo = SaveFunctions();
    if (!ch || !cdo || count <= 0) return false;
    const int before = CountOf(path);
    if (before < count) return false;
    SDK::Params::SaveFunctions_C_TryConsumingItemFromInventory p{};
    SetSoft(p, path);
    p.Count = count;
    p.__WorldContext = ch;
    if (!Call(cdo, "TryConsumingItemFromInventory", p) || !p.Success) return false;
    const int after = CountOf(path);
    if (after > before - count) {   // the game took one per call: take the rest one at a time
        for (int i = after; i > before - count; --i) {
            SDK::Params::SaveFunctions_C_TryConsumingItemFromInventory q{};
            SetSoft(q, path);
            q.Count = 1;
            q.__WorldContext = ch;
            if (!Call(cdo, "TryConsumingItemFromInventory", q) || !q.Success) break;
        }
    }
    return CountOf(path) == before - count;
}

// ------------------------------------------------------------------ revive

bool TeammateCanRevive(bool anyArea = false) {
    auto local = coop::LocalSlot();
    if (!local || !coop::InSession()) return false;
    for (const auto& m : coop::Members()) {
        if (!m.present || m.isLocal || m.slot == *local || S().downed[m.slot]) continue;
        bool dead = false, sameArea = false;
        if (RemotePlayerInfo(m.slot, dead, sameArea) && !dead && (sameArea || anyArea)) return true;
    }
    return false;
}

void SetDowned(bool down) {
    auto& s = S();
    if (s.localDowned == down) return;
    s.localDowned = down;
    s.downedAt = Now();
    s.downedChar = down ? static_cast<const void*>(ue::LocalCharacter()) : nullptr;
    s.noReviverSince = -1;
    s.revivingMe = -1;
    s.givenUp = false;
    s.giveUpHoldSince = -1;
    s.giveUpProgress = 0;
    if (auto slot = coop::LocalSlot()) s.downed[*slot] = down;
    net::MsgParty m;
    m.kind = net::PartyKind::Downed;
    m.arg = down ? 1 : 0;
    if (SDK::ACharacter* ch = ue::LocalCharacter(); ch && ue::IsValid(ch)) m.pos = V(ch->K2_GetActorLocation());
    Send(std::move(m));
}

void GoDown(SDK::AFS_PlayerChar_C* p) {
    QD_INFO(kTag, "down instead of dead (a teammate can revive)");
    if (p->CharacterMovement && ue::IsValid(p->CharacterMovement)) p->CharacterMovement->StopMovementImmediately();
    SetDowned(true);
    ui::Toast(ui::Tr("party.down"), ui::ToastKind::Warning, 8.0f);
}

// The game's own death, for real (nobody can revive us).
void DieForReal(const char* why) {
    auto& s = S();
    SDK::AFS_PlayerChar_C* p = LocalPlayer();
    SetDowned(false);
    if (!p || p->IsDead) return;
    QD_INFO(kTag, "down -> dead ({})", why);
    if (std::string_view(why) == "nobody left to revive") WorldSyncPartyWiped();
    SDK::Params::FS_PlayerChar_C_Kill k{};
    k.B33smentDeath = false;
    s.allowKill = true;
    Call(p, "Kill", k);
    s.allowKill = false;
}

void StandUp(uint8_t by) {
    auto& s = S();
    SDK::AFS_PlayerChar_C* p = LocalPlayer();
    if (!s.localDowned || !p) return;
    SetDowned(false);
    s.immuneUntil = Now() + kReviveImmuneSec;
    if (p->Health < 0) p->Health = 0;
    SDK::Params::FS_PlayerChar_C_HealForPercent h{};
    h.Percent = kReviveFraction;
    Call(p, "HealForPercent", h);
    QD_INFO(kTag, "revived by slot {} (health {:.0f})", by, p->Health);
    ui::Toast(ui::TrF("party.revived_by", NameOf(by)), ui::ToastKind::Success, 4.0f);
}

bool OnKillPre(hooks::CallInfo& ci) {
    auto& s = S();
    if (s.allowKill) return false;
    SDK::AFS_PlayerChar_C* p = LocalPlayer();
    if (!p || ci.object != p) return false;
    if (s.localDowned) return true;           // already down: stay down
    if (!s.downEnabled || !TeammateCanRevive()) {   // alone: the game's own death
        // 1.6.10: with every teammate down (or dead) that is a game over for the party, as "nobody left to revive" is:
        // the world goes back to the host's save (before, pickups taken since the save stayed gone)
        if (coop::InSession() && !TeammateCanRevive(true)) {
            bool anyMate = false;
            for (const auto& m : coop::Members())
                if (m.present && !m.isLocal) anyMate = true;
            if (anyMate) {
                QD_INFO(kTag, "dead with every teammate down: game over for the party");
                WorldSyncPartyWiped();
            }
        }
        return false;
    }
    GoDown(p);
    return true;
}

bool OnPlayerDamagePre(hooks::CallInfo& ci) {
    auto& s = S();
    if (!s.localDowned && Now() >= s.immuneUntil) return false;
    if (ci.object != ue::LocalCharacter()) return false;
    if (ci.params) static_cast<SDK::Params::FS_PlayerChar_C_TryApplyDamage*>(ci.params)->ReturnValue = false;
    if (ci.result) *static_cast<bool*>(ci.result) = false;
    return true;
}

bool OnBlockedInputPre(hooks::CallInfo& ci) {
    return S().localDowned && ci.object == ue::LocalCharacter();
}

bool InteractHeld(SDK::ACharacter* ch) {
    if (S().forceHold) return true;
    SDK::UClass* cls = SDK::UFB_InputSubsystem::StaticClass();
    SDK::UObject* cdo = cls ? cls->ClassDefaultObject : nullptr;
    if (!cdo) return false;
    static SDK::UFunction* fn = nullptr;
    if (!ue::IsValid(fn)) fn = ue::FindFunction(cdo, "IsInputActionKeyDown");
    if (!fn) return false;
    SDK::Params::FB_InputSubsystem_IsInputActionKeyDown p{};
    p.WorldContextObject = ch;
    p.Action = ue::MakeName("Interact");
    ue::CallFunction(cdo, fn, &p);
    return p.ReturnValue;
}

// ---- when the prompts may show: only in normal play, only for the teammate the player is looking at

bool g_uiMode = false, g_dialogue = false, g_inputLocked = false;
SDK::UObject* g_flagsFor = nullptr;   // the character the flags above belong to

bool GameBusy(SDK::AFS_PlayerChar_C* p) {
    if (!p) return true;
    if (p != g_flagsFor) {   // a new character (level load): the game starts it in normal play
        g_flagsFor = p;
        g_uiMode = g_dialogue = g_inputLocked = false;
    }
    if (p->InIntroCutscene) return true;
    if (p->CurrentCutsceneManager && ue::IsValid(reinterpret_cast<const SDK::UObject*>(p->CurrentCutsceneManager))) return true;
    return g_uiMode || g_dialogue || g_inputLocked || ui::IsMenuOpen();
}

void OnPlayerFlag(hooks::CallInfo& ci, bool* flag, bool value) {
    if (ci.object && ci.object == ue::LocalCharacter()) *flag = value;
}

// The camera looks at `target` (within `radius` cm of the line of sight, a little more far away).
bool LookingAt(SDK::AFS_PlayerChar_C* p, const SDK::FVector& target, float radius) {
    SDK::UCameraComponent* c = p ? p->FirstPersonCamera : nullptr;
    if (!c || !ue::IsValid(c)) return false;
    const SDK::FVector eye = c->K2_GetComponentLocation();
    const SDK::FVector f = c->GetForwardVector();
    const float dx = target.X - eye.X, dy = target.Y - eye.Y, dz = target.Z - eye.Z;
    const float along = dx * f.X + dy * f.Y + dz * f.Z;
    if (along < 20.0f) return false;
    const float px = dx - f.X * along, py = dy - f.Y * along, pz = dz - f.Z * along;
    return std::sqrt(px * px + py * py + pz * pz) <= radius + along * 0.05f;
}

void StopReviving() {
    auto& s = S();
    if (s.reviveTarget >= 0) SendTo(static_cast<uint8_t>(s.reviveTarget), net::PartyKind::Reviving, 0, 0);
    s.reviveTarget = -1;
    s.reviveProgress = 0;
}

void TickRevive(double now) {
    auto& s = S();
    SDK::AFS_PlayerChar_C* p = LocalPlayer();
    if (!coop::InSession()) {
        if (s.localDowned) DieForReal("session ended");
        s.reviveTarget = -1;
        s.reviveCandidate = -1;
        return;
    }
    if (!p) return;

    if (s.localDowned) {
        // 1.6.7: quitting to the main menu while down kept the flag, and the next load's fresh character was killed
        // at once ("nobody left to revive"; play test: a black screen after loading, "can't teleport while dead").
        // A character other than the one that went down is a new life: it stands.
        if (static_cast<const void*>(p) != s.downedChar) {
            QD_INFO(kTag, "down state dropped: a new character (the world was left or reloaded while down)");
            SetDowned(false);
            return;
        }
        if (p->IsDead) {   // the game killed us another way (a pit, a reload)
            SetDowned(false);
            return;
        }
        // give up: hold Interact (only while nobody is reviving us)
        if (!s.givenUp) {
            if (s.revivingMe < 0 && !GameBusy(p) && InteractHeld(p)) {
                if (s.giveUpHoldSince < 0) s.giveUpHoldSince = now;
                s.giveUpProgress = static_cast<float>(std::clamp((now - s.giveUpHoldSince) / kGiveUpSec, 0.0, 1.0));
                if (s.giveUpProgress >= 1.0f) {
                    s.givenUp = true;
                    s.giveUpHoldSince = -1;
                    s.giveUpProgress = 0;
                    QD_INFO(kTag, "gave up: down until a teammate rests or saves at a checkpoint");
                    ui::Toast(ui::Tr("party.gave_up"), ui::ToastKind::Info, 8.0f);
                    net::MsgParty g;
                    g.kind = net::PartyKind::Downed;
                    g.arg = 2;   // still down, gave up
                    g.pos = V(p->K2_GetActorLocation());
                    Send(std::move(g));
                }
            } else {
                s.giveUpHoldSince = -1;
                s.giveUpProgress = 0;
            }
        }
        // given up: the teammate is on the way to a checkpoint, maybe in another area; only a party with nobody
        // standing is over
        if (TeammateCanRevive(s.givenUp)) s.noReviverSince = -1;
        else if (s.noReviverSince < 0) s.noReviverSince = now;
        else if (now - s.noReviverSince > kBleedOutGraceSec) DieForReal("nobody left to revive");
        return;
    }

    // reviver side: the nearest downed teammate in reach
    s.reviveCandidate = -1;
    if (p->IsDead) {
        StopReviving();
        return;
    }
    const SDK::FVector me = p->K2_GetActorLocation();
    float best = kReviveRangeCm;
    const bool busy = GameBusy(p);
    for (const auto& m : coop::Members()) {
        if (busy) break;
        if (!m.present || m.isLocal || !s.downed[m.slot]) continue;
        net::Vec3f feet;
        float yaw = 0;
        if (!playersync::RemotePose(m.slot, feet, yaw)) continue;
        const float dx = feet.x - me.X, dy = feet.y - me.Y;
        const float d = std::sqrt(dx * dx + dy * dy);
        if (d > best || std::fabs(feet.z - me.Z) >= kReviveZRangeCm) continue;
        // the body lies on the ground: aim anywhere at it (a revive already going keeps its target)
        if (m.slot != s.reviveTarget && !LookingAt(p, SDK::FVector{feet.x, feet.y, feet.z + 25.0f}, 90.0f)) continue;
        best = d;
        s.reviveCandidate = m.slot;
    }
    const bool held = s.reviveCandidate >= 0 && InteractHeld(p);
    if (!held || s.reviveCandidate != s.reviveTarget || p->Health < s.reviveHpAtStart - 0.01f) {
        if (s.reviveTarget >= 0) StopReviving();
        if (!held) return;
        s.reviveTarget = s.reviveCandidate;
        s.reviveStart = now;
        s.reviveHpAtStart = p->Health;
        SendTo(static_cast<uint8_t>(s.reviveTarget), net::PartyKind::Reviving, 0, 1);
        QD_INFO(kTag, "reviving slot {}", s.reviveTarget);
    }
    s.reviveHpAtStart = std::min(s.reviveHpAtStart, p->Health);
    s.reviveProgress = static_cast<float>(std::clamp((now - s.reviveStart) / kReviveSec, 0.0, 1.0));
    if (s.reviveProgress >= 1.0f) {
        const uint8_t t = static_cast<uint8_t>(s.reviveTarget);
        SendTo(t, net::PartyKind::Revive);
        s.downed[t] = false;   // until their own word arrives
        s.reviveTarget = -1;
        s.reviveProgress = 0;
        QD_INFO(kTag, "revived slot {}", t);
        ui::Toast(ui::TrF("party.you_revived", NameOf(t)), ui::ToastKind::Success, 3.0f);
    }
}

// ------------------------------------------------------------------ party rest

SDK::USaveMenu_C* OpenSaveMenu() {
    SDK::UClass* cls = ue::FindClass(kSaveMenuClass);
    if (!cls) return nullptr;
    auto& objs = SDK::UObject::GObjects;
    for (int i = 0; i < objs->Num(); ++i) {
        SDK::UObject* o = objs->GetByIndex(i);
        if (!o || o->IsDefaultObject() || !o->IsA(cls) || !ue::IsValid(o)) continue;
        auto* m = static_cast<SDK::USaveMenu_C*>(o);
        if (m->Removed) continue;
        SDK::UFunction* fn = ue::FindFunction(m, "IsInViewport");
        SDK::Params::UserWidget_IsInViewport p{};
        if (fn) ue::CallFunction(m, fn, &p);
        if (!fn || p.ReturnValue) return m;
    }
    return nullptr;
}

bool LevelsBusy() {
    SDK::UObject* cdo = SDK::AFS_LevelManager::GetDefaultObj();
    SDK::UWorld* world = ue::World();
    if (!cdo || !world) return false;
    SDK::UFunction* fn = ue::FindFunction(cdo, "IsProcessingLevelChanges");
    if (!fn) return false;
    SDK::Params::FS_LevelManager_IsProcessingLevelChanges p{};
    p.WorldContextObject = world;
    ue::CallFunction(cdo, fn, &p);
    return p.ReturnValue;
}

// A player pressed Rest or Save in a checkpoint's menu.
void OnMenuButton(hooks::CallInfo& ci, bool save) {
    auto& s = S();
    if (s.driving || !coop::InSession()) return;
    auto* menu = static_cast<SDK::USaveMenu_C*>(ci.object);
    if (!menu || !ue::IsValid(menu) || !ue::IsValid(menu->Checkpoint)) return;
    SDK::Params::Checkpoint_C_GetLocationData ld{};
    if (!Call(menu->Checkpoint, "GetLocationData", ld) || !ld.ReturnValue) {
        QD_WARN(kTag, "party {}: the checkpoint has no location data", save ? "save" : "rest");
        return;
    }
    net::MsgParty m;
    m.kind = net::PartyKind::Rest;
    m.arg = save ? 1 : 0;
    m.id = ++s.restSeq;
    m.text = ue::PathName(ld.ReturnValue);
    m.pos = V(menu->Checkpoint->K2_GetActorLocation());
    QD_INFO(kTag, "party {} at {}", save ? "save" : "rest", m.text);
    Send(std::move(m));
    ui::Toast(ui::Tr(save ? "party.save_sent" : "party.rest_sent"), ui::ToastKind::Info, 3.0f);
}

// The game's own teleport ending, as F5 > Fix > Unstuck does (a teleport that never finished left the player with no
// input: 1.5.4, a party save from far away).
void EndStuckTeleport(SDK::AFS_PlayerChar_C* p) {
    if (!p || !ue::IsValid(p)) return;
    char none = 0;
    if (p->Teleporting || p->QuickTeleporting) {
        Call(p, "FinishTeleport", none);
        Call(p, "CleanUpTeleportProcess", none);
    }
    Call(p, "UnlockInputForLoadingOrCutscene", none);
    Call(p, "SlightDelayEnableInput", none);
}

void RestFail(const char* key) {
    auto& s = S();
    QD_WARN(kTag, "party rest from slot {} not followed: {}", s.rest.from, key);
    if (s.rest.step == RestStep::Travel) EndStuckTeleport(LocalPlayer());
    ui::Toast(ui::TrF(key, NameOf(s.rest.from)), ui::ToastKind::Warning, 5.0f);
    s.rest.step = RestStep::Idle;
    s.driving = false;
}

void StartRest(const net::MsgParty& m) {
    auto& s = S();
    SDK::AFS_PlayerChar_C* p = LocalPlayer();
    s.rest = RestJob{};
    s.rest.from = m.fromSlot;
    s.rest.save = m.arg == 1;
    s.rest.locPath = m.text;
    s.rest.pos = V(m.pos);
    if (!p || p->IsDead) return RestFail("party.rest_missed_dead");
    if (p->CurrentCutsceneManager || p->InIntroCutscene) return RestFail("party.rest_missed_busy");
    if (p->Resting || OpenSaveMenu()) return RestFail("party.rest_missed_busy");
    if (s.localDowned) {   // a rest stands a downed player up
        SetDowned(false);
        if (p->Health < 0) p->Health = 0;
        SDK::Params::FS_PlayerChar_C_HealForPercent h{};
        h.Percent = kReviveFraction;
        Call(p, "HealForPercent", h);
    }
    if (s.trade.stage != TradeStage::None) {
        SendTo(s.trade.peer, net::PartyKind::TradeCancel, s.trade.id);
        s.trade = Trade{};
    }
    ui::Toast(ui::TrF(s.rest.save ? "party.save_follow" : "party.rest_follow", NameOf(m.fromSlot)), ui::ToastKind::Info, 4.0f);
    s.rest.stepAt = Now();
    if (Dist(p->K2_GetActorLocation(), s.rest.pos) <= kRestNearCm) {
        s.rest.step = RestStep::FindCheckpoint;
        return;
    }
    auto* loc = static_cast<SDK::UFS_LocationData*>(ue::LoadObject(s.rest.locPath));
    if (!loc) return RestFail("party.rest_missed_far");
    SDK::Params::FS_PlayerChar_C_TeleportToLocation t{};
    t.Location_Data = loc;
    t.GreenFX = false;
    if (!Call(p, "TeleportToLocation", t)) return RestFail("party.rest_missed_far");
    QD_INFO(kTag, "party rest: travelling to {}", s.rest.locPath);
    s.rest.step = RestStep::Travel;
}

SDK::ACheckpoint_C* NearestCheckpoint(const SDK::FVector& at, float maxCm) {
    SDK::UClass* cls = ue::FindClass(kCheckpointClass);
    if (!cls) return nullptr;
    std::vector<SDK::AActor*> all;
    ActorsOfClass(cls, all);
    SDK::ACheckpoint_C* best = nullptr;
    float bestD = maxCm;
    for (SDK::AActor* a : all) {
        if (!a || !ue::IsValid(a)) continue;
        const float d = Dist(a->K2_GetActorLocation(), at);
        if (d < bestD) {
            bestD = d;
            best = static_cast<SDK::ACheckpoint_C*>(a);
        }
    }
    return best;
}

void TickRest(double now) {
    auto& s = S();
    RestJob& r = s.rest;
    if (r.step == RestStep::Idle) return;
    SDK::AFS_PlayerChar_C* p = LocalPlayer();
    if (!p) {
        if (now - r.stepAt > kRestTravelMaxSec) RestFail("party.rest_missed_far");
        return;   // a level load during the travel: wait for the character
    }
    const double inStep = now - r.stepAt;
    auto next = [&](RestStep st) {
        r.step = st;
        r.stepAt = now;
    };
    switch (r.step) {
        case RestStep::Travel:
            if (inStep < 1.5 || p->Teleporting || p->QuickTeleporting || LevelsBusy()) {
                if (inStep > kRestTravelMaxSec) RestFail("party.rest_missed_far");
                return;
            }
            next(RestStep::FindCheckpoint);
            return;
        case RestStep::FindCheckpoint: {
            r.checkpoint = NearestCheckpoint(r.pos, 2500.0f);
            if (!r.checkpoint) {
                if (inStep > 10.0) RestFail("party.rest_missed_far");
                return;
            }
            const SDK::FVector cp = r.checkpoint->K2_GetActorLocation();
            if (Dist(p->K2_GetActorLocation(), cp) > 400.0f) {
                const float half = p->CapsuleComponent ? p->CapsuleComponent->GetScaledCapsuleHalfHeight() : 90.0f;
                const SDK::FVector dest(cp.X + 180.0f, cp.Y, cp.Z + half + 20.0f);
                p->K2_TeleportTo(dest, p->K2_GetActorRotation());
            }
            next(RestStep::OpenMenu);
            return;
        }
        case RestStep::OpenMenu:
            if (inStep < 0.3) return;
            if (!ue::IsValid(r.checkpoint)) return RestFail("party.rest_missed_far");
            if (!OpenSaveMenu()) {
                s.driving = true;
                SDK::Params::Checkpoint_C_Interact in{};
                in.PlayerInteracting = p;
                Call(r.checkpoint, "Interact", in);
            }
            next(RestStep::Press);
            return;
        case RestStep::Press: {
            SDK::USaveMenu_C* menu = OpenSaveMenu();
            if (!menu) {
                if (inStep > kRestMenuMaxSec) RestFail("party.rest_missed_busy");
                return;
            }
            if (inStep < 0.6) return;   // let the menu finish opening
            struct { SDK::UCommonButtonBase* Button = nullptr; } btn;
            s.driving = true;
            Call(menu, r.save ? kSaveButtonName : kRestButtonName, btn);
            r.sawResting = false;
            next(RestStep::WaitDone);
            return;
        }
        case RestStep::WaitDone: {
            if (p->Resting) r.sawResting = true;
            SDK::USaveMenu_C* menu = OpenSaveMenu();
            bool done;
            if (r.save) done = inStep > 1.5 && menu && !menu->Saving;
            else done = inStep > 2.0 && !p->Resting && (r.sawResting || inStep > 6.0) && menu;
            if (!done && inStep < kRestRunMaxSec) return;
            next(RestStep::Close);
            return;
        }
        case RestStep::Close: {
            if (inStep < 0.4) return;
            if (SDK::USaveMenu_C* menu = OpenSaveMenu()) CallNoParams(menu, "Close");
            QD_INFO(kTag, "party {} done", r.save ? "save" : "rest");
            ui::Toast(ui::Tr(r.save ? "party.save_done" : "party.rest_done"), ui::ToastKind::Success, 3.0f);
            r.step = RestStep::Idle;
            s.driving = false;
            return;
        }
        default:
            r.step = RestStep::Idle;
            s.driving = false;
            return;
    }
}

// ------------------------------------------------------------------ trading

bool TeammateNear(uint8_t slot, float cm) {
    SDK::ACharacter* ch = ue::LocalCharacter();
    net::Vec3f feet;
    float yaw = 0;
    if (!ch || !ue::IsValid(ch) || !playersync::RemotePose(slot, feet, yaw)) return false;
    bool dead = false, sameArea = false;
    if (!RemotePlayerInfo(slot, dead, sameArea) || dead || !sameArea) return false;
    return Dist(ch->K2_GetActorLocation(), V(feet)) <= cm;
}

void TradeReset(const char* toastKey = nullptr, ui::ToastKind kind = ui::ToastKind::Info) {
    auto& s = S();
    if (toastKey && s.trade.peer < net::kMaxPlayers) ui::Toast(ui::TrF(toastKey, NameOf(s.trade.peer)), kind, 4.0f);
    s.trade = Trade{};
}

void SendOffer() {
    auto& s = S();
    net::MsgParty m;
    m.kind = net::PartyKind::TradeOffer;
    m.toSlot = s.trade.peer;
    m.id = s.trade.id;
    m.a = s.trade.myVersion;
    for (const auto& [path, n] : s.trade.mine)
        if (n > 0) m.items.push_back({path, n});
    Send(std::move(m));
}

// Game thread, from the menu.
void TradeRequest(uint8_t peer) {
    auto& s = S();
    if (s.trade.stage != TradeStage::None) return;
    if (s.localDowned || S().downed[peer]) return ui::Toast(ui::Tr("trade.not_down"), ui::ToastKind::Warning);
    if (!TeammateNear(peer, kTradeRangeCm)) return ui::Toast(ui::Tr("trade.too_far"), ui::ToastKind::Warning);
    static std::mt19937 rng{std::random_device{}()};
    s.trade = Trade{};
    s.trade.stage = TradeStage::Outgoing;
    s.trade.id = rng() | 1u;
    s.trade.peer = peer;
    s.trade.requester = true;
    s.trade.openedAt = Now();
    SendTo(peer, net::PartyKind::TradeRequest, s.trade.id);
    ui::Toast(ui::TrF("trade.requested", NameOf(peer)), ui::ToastKind::Info);
}
void TradeAnswer(bool accept) {
    auto& s = S();
    if (s.trade.stage != TradeStage::Incoming) return;
    if (!accept) {
        SendTo(s.trade.peer, net::PartyKind::TradeDecline, s.trade.id);
        return TradeReset();
    }
    if (!TeammateNear(s.trade.peer, kTradeRangeCm)) {
        SendTo(s.trade.peer, net::PartyKind::TradeDecline, s.trade.id);
        ui::Toast(ui::Tr("trade.too_far"), ui::ToastKind::Warning);
        return TradeReset();
    }
    s.trade.stage = TradeStage::Open;
    SendTo(s.trade.peer, net::PartyKind::TradeAccept, s.trade.id);
}
void TradeCancel() {
    auto& s = S();
    if (s.trade.stage == TradeStage::None || s.trade.committing) return;
    SendTo(s.trade.peer, net::PartyKind::TradeCancel, s.trade.id);
    TradeReset("trade.cancelled_by_you");
}
void TradeSetCount(const std::string& path, int count) {
    auto& s = S();
    if (s.trade.stage != TradeStage::Open || s.trade.committing) return;
    count = std::clamp(count, 0, CountOf(path));
    int& cur = s.trade.mine[path];
    if (cur == count) return;
    cur = count;
    ++s.trade.myVersion;
    s.trade.myConfirm = s.trade.theirConfirm = false;
    SendOffer();
}
void TradeConfirm(bool yes) {
    auto& s = S();
    if (s.trade.stage != TradeStage::Open || s.trade.committing) return;
    s.trade.myConfirm = yes;
    SendTo(s.trade.peer, net::PartyKind::TradeConfirm, s.trade.id, yes ? 1 : 0, s.trade.theirVersion);
}

bool OfferAvailable() {
    for (const auto& [path, n] : S().trade.mine)
        if (n > 0 && CountOf(path) < n) return false;
    return true;
}
bool HandOver(const std::vector<OfferLine>& get) {
    auto& s = S();
    for (const auto& [path, n] : s.trade.mine)
        if (n > 0 && !TakeItem(path, n)) {
            QD_WARN(kTag, "trade: could not take {} x{}", path, n);
            return false;
        }
    for (const auto& l : get)
        if (!GiveItem(l.path, l.count)) QD_WARN(kTag, "trade: could not add {} x{}", l.path, l.count);
    return true;
}

// Trade prompt: next to a teammate (both up, no trade going), hold Interact for kTradeHoldSec to ask them to trade;
// holding it next to a teammate who asked accepts. The name plate shows the hint and the progress.
constexpr float  kTradePromptCm = 260.0f;
constexpr double kTradeHoldSec = 0.8;
constexpr double kTradeLookSec = 0.7;   // looking at the teammate this long shows the prompt
constexpr double kTradeLingerSec = 0.3;  // a glance away this short keeps it
struct TradePrompt {
    int    cand = -1;          // the teammate the prompt is for
    double start = -1;         // Interact held since
    float  progress = 0;
    bool   latch = false;      // fired: wait for Interact to be released
    int    looked = -1;        // the teammate in the crosshair
    double lookSince = -1;     // ... since
    double lastSeen = -1;      // last time the shown teammate was in the crosshair
    int    shown = -1;         // the teammate the prompt is shown for
};
TradePrompt g_tp;
double g_revivingSince = 0;   // when the teammate reviving us started (our own estimate of their progress)

// First person: on our own screen "lying down" is the camera on the ground, tilted. Eased in and out; the camera's
// own offsets are put back when we stand up (or the character changes).
struct DownView {
    SDK::UCameraComponent* cam = nullptr;
    SDK::FVector baseLoc{};
    SDK::FRotator baseRot{};
    float blend = 0;
    double lastAt = 0;
} g_dv;

void TickDownedView(double now) {
    auto& s = S();
    SDK::AFS_PlayerChar_C* p = LocalPlayer();
    const double dt = g_dv.lastAt > 0 ? std::clamp(now - g_dv.lastAt, 0.0, 0.1) : 0.0;
    g_dv.lastAt = now;
    SDK::UCameraComponent* c = p ? p->FirstPersonCamera : nullptr;
    if (!c || !ue::IsValid(c)) {
        g_dv.cam = nullptr;
        g_dv.blend = 0;
        return;
    }
    const float target = s.localDowned && !p->IsDead ? 1.0f : 0.0f;
    if (g_dv.cam != c) {
        if (target == 0.0f) return;   // nothing to undo, nothing to do
        g_dv.cam = c;
        g_dv.baseLoc = c->RelativeLocation;
        g_dv.baseRot = c->RelativeRotation;
        g_dv.blend = 0;
    }
    const float speed = target > g_dv.blend ? 2.5f : 3.5f;   // fall in 0.4 s, get up a little faster
    g_dv.blend = target > g_dv.blend ? std::min(target, g_dv.blend + speed * static_cast<float>(dt))
                                     : std::max(target, g_dv.blend - speed * static_cast<float>(dt));
    const float e = g_dv.blend * g_dv.blend * (3.0f - 2.0f * g_dv.blend);
    SDK::FVector loc = g_dv.baseLoc;
    loc.Z -= 125.0f * e;
    SDK::FRotator rot = g_dv.baseRot;
    rot.Roll += 30.0f * e;
    SDK::FHitResult hit{};
    c->K2_SetRelativeLocationAndRotation(loc, rot, false, &hit, true);
    if (g_dv.blend <= 0.0f && target == 0.0f) g_dv.cam = nullptr;   // back where it was: the game owns it again
}

std::string UiNameOf(uint8_t slot);

// The line (and bar) in the middle of the screen.
void BuildPrompt(UiSnap& snap, double now) {
    auto& s = S();
    if (!snap.inSession || !snap.inGame || GameBusy(LocalPlayer())) return;
    if (s.localDowned) {
        if (s.revivingMe >= 0) {
            snap.prompt = ui::TrF("party.hud_being_revived", UiNameOf(static_cast<uint8_t>(s.revivingMe)));
            snap.progress = static_cast<float>(std::clamp((now - g_revivingSince) / kReviveSec, 0.0, 1.0));
        } else if (s.givenUp) {
            snap.prompt = ui::Tr("party.hud_gave_up");
        } else {
            snap.prompt = ui::Tr("party.hud_down");
            if (s.giveUpProgress > 0) snap.progress = s.giveUpProgress;
        }
        return;
    }
    if (s.reviveTarget >= 0) {
        snap.prompt = ui::TrF("party.hud_reviving", UiNameOf(static_cast<uint8_t>(s.reviveTarget)));
        snap.progress = s.reviveProgress;
        return;
    }
    if (s.reviveCandidate >= 0) {
        snap.prompt = ui::TrF("party.hud_hold_revive", UiNameOf(static_cast<uint8_t>(s.reviveCandidate)));
        return;
    }
    if (g_tp.cand >= 0) {
        const bool accept = s.trade.stage == TradeStage::Incoming && s.trade.peer == g_tp.cand;
        snap.prompt = ui::TrF(accept ? "party.hud_hold_accept" : "party.hud_hold_trade", UiNameOf(static_cast<uint8_t>(g_tp.cand)));
        if (g_tp.progress > 0) snap.progress = g_tp.progress;
        return;
    }
    // a trade request waiting for an answer stays on the screen until it is answered (or runs out)
    if (s.trade.stage == TradeStage::Incoming)
        snap.prompt = ui::TrF("party.hud_trade_waiting", UiNameOf(s.trade.peer));
}

void TickTradePrompt(double now) {
    auto& s = S();
    SDK::AFS_PlayerChar_C* p = LocalPlayer();
    g_tp.cand = -1;
    if (!p || p->IsDead || s.localDowned || !coop::InSession() || s.reviveCandidate >= 0 || s.trade.stage == TradeStage::Open ||
        s.trade.stage == TradeStage::Outgoing || GameBusy(p)) {
        g_tp.start = -1;
        g_tp.progress = 0;
        g_tp.looked = g_tp.shown = -1;
        g_tp.lookSince = -1;
        return;
    }
    int looked = -1;
    float best = kTradePromptCm + 1;
    for (const auto& m : coop::Members()) {
        if (!m.present || m.isLocal || s.downed[m.slot]) continue;
        if (s.trade.stage == TradeStage::Incoming && m.slot != s.trade.peer) continue;
        net::Vec3f feet;
        float yaw = 0;
        if (!TeammateNear(m.slot, kTradePromptCm) || !playersync::RemotePose(m.slot, feet, yaw)) continue;
        const float d = Dist(p->K2_GetActorLocation(), V(feet));
        if (d >= best || !LookingAt(p, SDK::FVector{feet.x, feet.y, feet.z + 100.0f}, 55.0f)) continue;
        best = d;
        looked = m.slot;
    }
    // the prompt shows after looking at the same teammate for kTradeLookSec, and stays through a short glance away
    if (looked != g_tp.looked) {
        g_tp.looked = looked;
        g_tp.lookSince = looked >= 0 ? now : -1;
    }
    if (looked >= 0 && now - g_tp.lookSince >= kTradeLookSec) g_tp.shown = looked;
    if (looked >= 0 && looked == g_tp.shown) g_tp.lastSeen = now;
    if (g_tp.shown >= 0 && looked != g_tp.shown && now - g_tp.lastSeen > kTradeLingerSec && g_tp.start < 0) g_tp.shown = -1;
    g_tp.cand = g_tp.shown;
    const bool held = g_tp.cand >= 0 && InteractHeld(p);
    if (!held) {
        g_tp.latch = false;
        g_tp.start = -1;
        g_tp.progress = 0;
        return;
    }
    if (g_tp.latch) return;
    if (g_tp.start < 0) g_tp.start = now;
    g_tp.progress = static_cast<float>(std::clamp((now - g_tp.start) / kTradeHoldSec, 0.0, 1.0));
    if (g_tp.progress < 1.0f) return;
    g_tp.latch = true;
    g_tp.start = -1;
    g_tp.progress = 0;
    if (s.trade.stage == TradeStage::Incoming && s.trade.peer == g_tp.cand) TradeAnswer(true);
    else if (s.trade.stage == TradeStage::None) TradeRequest(static_cast<uint8_t>(g_tp.cand));
}

std::string UiNameOf(uint8_t slot) { return NameOf(slot); }

// ---- watchdogs: nothing may leave the player without controls
std::atomic<double> g_tradeDrawnAt{0};   // the trade window was last drawn (render thread writes)
double g_stuckSince = -1, g_lockedSince = -1, g_tradeOpenSince = -1;

// The game's pick-up pause (ItemPickupAnimationComp.StartPickupPause): it turns the player's input off and pauses
// time, and EndPickupPause, run by the pickup itself at the end of its animation, turns them back on. A pickup that
// is gone before that (removed by the world sync, unloaded) leaves the player unable to walk, pause or open the menu.
SDK::UObject* g_pickupComp = nullptr;   // the animation component of the pickup being picked up
SDK::UObject* g_pickupOwner = nullptr;
double g_pickupSince = -1, g_pickupGoneSince = -1;

SDK::UObject* TimeDilationCdo() {
    SDK::UClass* c = ue::FindClass("/Game/LD45/Blueprints/TimeDilationManagement.TimeDilationManagement_C");
    return c ? c->ClassDefaultObject : nullptr;
}

// Everything StartPickupPause turned off, back on (what EndPickupPause does).
void EndStuckPickup(SDK::AFS_PlayerChar_C* p) {
    g_pickupComp = g_pickupOwner = nullptr;
    g_pickupSince = g_pickupGoneSince = -1;
    if (!p || !ue::IsValid(p)) return;
    if (SDK::APlayerController* pc = ue::LocalController(); pc && ue::IsValid(pc)) {
        p->EnableInput(pc);
        pc->ResetIgnoreInputFlags();
    }
    if (SDK::UObject* cdo = TimeDilationCdo()) {
        SDK::Params::TimeDilationManagement_C_Set_Pause_Time_Dilation tp{};
        tp.Pause = false;
        tp.__WorldContext = p;
        Call(cdo, "Set Pause Time Dilation", tp);
    }
    p->CustomTimeDilation = 1.0f;
    SDK::Params::FS_PlayerChar_C_SetPlayerDoingQuickInteraction qi{};
    qi.DoingIt = false;
    Call(p, "SetPlayerDoingQuickInteraction", qi);
}

void OnPickupPauseStart(hooks::CallInfo& ci) {
    g_pickupComp = ci.object;
    auto* comp = static_cast<SDK::UActorComponent*>(ci.object);
    g_pickupOwner = comp ? comp->GetOwner() : nullptr;
    g_pickupSince = Now();
    g_pickupGoneSince = -1;
}
void OnPickupPauseEnd(hooks::CallInfo& ci) {
    if (ci.object == g_pickupComp) {
        g_pickupComp = g_pickupOwner = nullptr;
        g_pickupSince = g_pickupGoneSince = -1;
    }
}

void TickWatchdogs(double now) {
    auto& s = S();
    SDK::AFS_PlayerChar_C* p = LocalPlayer();
    // a pick-up pause whose pickup went away (1 s), or one that never ended (20 s, outside dialogue and menus)
    if (g_pickupSince > 0) {
        const bool gone = !g_pickupComp || !ue::IsValid(g_pickupComp) || !g_pickupOwner || !ue::IsValid(g_pickupOwner);
        if (gone && g_pickupGoneSince < 0) g_pickupGoneSince = now;
        const bool lost = gone && now - g_pickupGoneSince > 1.0;
        const bool tooLong = now - g_pickupSince > 20.0 && !g_dialogue && !g_uiMode && !ui::IsMenuOpen();
        if (lost || tooLong) {
            QD_WARN(kTag, "watchdog: a pick-up pause did not end ({}), giving the controls back", lost ? "the pickup is gone" : "20 s");
            EndStuckPickup(p);
        }
    }
    // a teleport of the game that never ends (it blocks all input)
    if (p && (p->Teleporting || p->QuickTeleporting)) {
        if (g_stuckSince < 0) g_stuckSince = now;
        if (now - g_stuckSince > 25.0) {
            QD_WARN(kTag, "watchdog: a teleport did not end in 25 s, ending it the game's own way");
            EndStuckTeleport(p);
            ui::Toast(ui::Tr("fix.auto_unstuck"), ui::ToastKind::Warning, 5.0f);
            g_stuckSince = -1;
        }
    } else {
        g_stuckSince = -1;
    }
    // input locked for loading or a cutscene, with neither going on
    const bool cutscene = p && (p->InIntroCutscene || p->CurrentCutsceneManager);
    if (p && g_inputLocked && !cutscene && !(p->Teleporting || p->QuickTeleporting)) {
        if (g_lockedSince < 0) g_lockedSince = now;
        if (now - g_lockedSince > 30.0) {
            QD_WARN(kTag, "watchdog: input locked for 30 s with no loading or cutscene, unlocking it");
            char none = 0;
            Call(p, "UnlockInputForLoadingOrCutscene", none);
            Call(p, "SlightDelayEnableInput", none);
            g_inputLocked = false;
            g_lockedSince = -1;
        }
    } else {
        g_lockedSince = -1;
    }
    // an open trade holds the input only while its window is on the screen
    if (s.trade.stage == TradeStage::Open) {
        if (g_tradeOpenSince < 0) g_tradeOpenSince = now;
        if (now - g_tradeOpenSince > 1.5 && now - g_tradeDrawnAt.load() > 1.5) {
            QD_WARN(kTag, "watchdog: the trade window is not showing, giving the controls back");
            SendTo(s.trade.peer, net::PartyKind::TradeCancel, s.trade.id);
            TradeReset("trade.cancelled_by_you");
            ui::SetInputCapture("trade", false);
        }
    } else {
        g_tradeOpenSince = -1;
    }
}

void TickTrade(double) {
    auto& s = S();
    Trade& t = s.trade;
    if (t.stage == TradeStage::None) return;
    const auto& mem = coop::Members();
    if (!coop::InSession() || t.peer >= mem.size() || !mem[t.peer].present) return TradeReset("trade.peer_left");
    if (t.stage == TradeStage::Open && !t.committing && !TeammateNear(t.peer, kTradeBreakCm)) {
        SendTo(t.peer, net::PartyKind::TradeCancel, t.id);
        return TradeReset("trade.too_far_cancel", ui::ToastKind::Warning);
    }
    if (t.stage == TradeStage::Outgoing && Now() - t.openedAt > 60.0) {
        SendTo(t.peer, net::PartyKind::TradeCancel, t.id);
        return TradeReset("trade.no_answer");
    }
    // requester: both confirmed the current offers -> commit
    if (t.stage == TradeStage::Open && t.requester && !t.committing && t.myConfirm && t.theirConfirm) {
        if (!OfferAvailable()) {
            t.myConfirm = false;
            TradeConfirm(false);
            ui::Toast(ui::Tr("trade.missing_items"), ui::ToastKind::Warning);
            return;
        }
        net::MsgParty m;
        m.kind = net::PartyKind::TradeCommit;
        m.toSlot = t.peer;
        m.id = t.id;
        m.a = t.theirVersion;
        for (const auto& [path, n] : t.mine)
            if (n > 0) m.items.push_back({path, n});
        t.committing = true;
        Send(std::move(m));
    }
}

void OnTradeMsg(const net::MsgParty& m) {
    auto& s = S();
    Trade& t = s.trade;
    using K = net::PartyKind;
    if (m.kind == K::TradeRequest) {
        if (t.stage != TradeStage::None || s.localDowned) {
            SendTo(m.fromSlot, K::TradeDecline, m.id, 1);
            return;
        }
        t = Trade{};
        t.stage = TradeStage::Incoming;
        t.id = m.id;
        t.peer = m.fromSlot;
        t.openedAt = Now();
        ui::Toast(ui::TrF("trade.incoming", NameOf(m.fromSlot)), ui::ToastKind::Info, 6.0f);
        return;
    }
    if (t.stage == TradeStage::None || m.id != t.id || m.fromSlot != t.peer) return;
    switch (m.kind) {
        case K::TradeAccept:
            if (t.stage == TradeStage::Outgoing) t.stage = TradeStage::Open;
            return;
        case K::TradeDecline:
            return TradeReset(m.arg == 1 ? "trade.busy" : "trade.declined");
        case K::TradeCancel:
            return TradeReset("trade.cancelled");
        case K::TradeOffer:
            t.theirs.clear();
            for (const auto& it : m.items) {
                OfferLine l{it.path, it.path, it.count};
                if (SDK::UObject* o = ue::LoadObject(it.path)) {
                    auto* d = static_cast<SDK::UFS_ItemData_C*>(o);
                    if (d->Name_0.TextData) l.name = d->Name_0.ToString();
                }
                t.theirs.push_back(std::move(l));
            }
            t.theirVersion = m.a;
            t.myConfirm = t.theirConfirm = false;
            return;
        case K::TradeConfirm:
            if (m.a == t.myVersion) t.theirConfirm = m.arg == 1;
            return;
        case K::TradeCommit: {   // we are not the requester: hand over now
            std::vector<OfferLine> get;
            for (const auto& it : m.items) get.push_back({it.path, it.path, it.count});
            const bool ok = !t.requester && m.a == t.myVersion && t.myConfirm && OfferAvailable() && HandOver(get);
            SendTo(t.peer, K::TradeDone, t.id, ok ? 1 : 0);
            if (ok) return TradeReset("trade.done", ui::ToastKind::Success);
            t.myConfirm = t.theirConfirm = false;
            ui::Toast(ui::Tr("trade.failed"), ui::ToastKind::Warning);
            return;
        }
        case K::TradeDone:
            if (!t.committing) return;
            t.committing = false;
            if (m.arg == 1) {
                HandOver(t.theirs);
                return TradeReset("trade.done", ui::ToastKind::Success);
            }
            t.myConfirm = t.theirConfirm = false;
            ui::Toast(ui::Tr("trade.failed"), ui::ToastKind::Warning);
            return;
        default:
            return;
    }
}

// ------------------------------------------------------------------ message handler

// ------------------------------------------------------------------ shared items
//
// Every item a player gets that is not a consumable is given to every other player too: weapons, gear, necklaces,
// belts, upgrades, keys, treasures; picked up, from an NPC, a chest, an altar, a boss. Shop purchases stay with the
// buyer, and consumables and coins are traded by hand. Items pass through the game's SaveFunctions.AddItemToInventory,
// hooked here. A one-of-a-kind item (weapon, equippable, unstackable) is given only to a player who does not have it
// yet; anything else in the same amount.
constexpr const char* kCoinItem = "/Game/Items/Treasure/Coins/Coin.Coin";

bool Shared(const SDK::UFS_ItemData_C* item, const std::string& path) {
    return item && !item->Consumable && path != kCoinItem;
}
bool OneOfAKind(const SDK::UFS_ItemData_C* item) {
    return item->Unstackable;   // a stack (daggers) shares its amount
}

constexpr uint64_t kSettleMs = 0;   // a weapon is never given twice, so the starting inventory needs no wait
SDK::UObject* g_lastChar = nullptr;
uint64_t g_charSince = 0;

bool ShopOpen() {
    auto& objs = SDK::UObject::GObjects;
    for (int i = 0; i < objs->Num(); ++i) {
        SDK::UObject* o = objs->GetByIndex(i);
        if (!o || o->IsDefaultObject() || !o->Class || ue::Name(o->Class) != "ShopMenu_C" || !ue::IsValid(o)) continue;
        struct { bool v; } p{false};
        if (SDK::UFunction* f = ue::FindFunction(o, "IsInViewport")) ue::CallFunction(o, f, &p);
        if (p.v) return true;
    }
    return false;
}

std::string ShortItemName(const std::string& path) {
    const size_t dot = path.rfind('.');
    return dot == std::string::npos ? path : path.substr(dot + 1);
}

// ---- catch-up: the host keeps, per save slot, the list of one-of-a-kind items shared in that world. A friend who
// joins later (or missed one) is given every listed item they do not have. A save from before this list existed starts
// it from the host's own one-of-a-kind, non-consumable items.

// path -> the party's agreed amount of that shared item in this world. It only ever grows (a player who lost items, a
// fresh character, never drags everyone down); a catch-up brings each player up to it.
std::map<std::string, int> g_ledger;
int g_ledgerSlot = -1000;

int HostSaveSlot() {
    const int s = QueryActiveSaveSlot();
    return s >= 0 ? s : coop::ActiveSaveSlotCached();
}

std::filesystem::path LedgerPath(int slot) {
    return std::filesystem::path(paths::ModSaved()) / L"shared" / (L"slot" + std::to_wstring(slot) + L".txt");
}

void LedgerSave() {
    std::error_code ec;
    const auto p = LedgerPath(g_ledgerSlot);
    std::filesystem::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::trunc);
    for (const auto& [path, n] : g_ledger) f << path << "\t" << n << "\n";
}

// A correction file next to the list (slot<n>.add.txt, "path<TAB>amount" lines) raises agreed amounts once and is then
// removed: how a lost amount is put right without touching the running game.
void MergeCorrections(int slot) {
    std::error_code ec;
    const auto add = LedgerPath(slot).replace_extension(L".add.txt");
    if (!std::filesystem::is_regular_file(add, ec)) return;
    int n = 0;
    {
        std::ifstream f(add);
        for (std::string line; std::getline(f, line);) {
            const size_t tab = line.find('	');
            if (line.empty() || tab == std::string::npos) continue;
            const std::string path = line.substr(0, tab);
            const int amount = std::atoi(line.c_str() + tab + 1);
            if (amount > 0 && amount > g_ledger[path]) {
                g_ledger[path] = amount;
                ++n;
            }
        }
    }
    std::filesystem::remove(add, ec);
    LedgerSave();
    QD_INFO(kTag, "shared-items list of slot {}: {} amount(s) corrected", slot, n);
}

// The list of the host's current save, loaded (or started) when the save changes. False when not in a game.
bool LedgerReady() {
    if (!coop::IsHost() || !LocalPlayer()) return false;
    const int slot = HostSaveSlot();
    if (slot == g_ledgerSlot) return true;
    g_ledgerSlot = slot;
    g_ledger.clear();
    std::error_code ec;
    const auto p = LedgerPath(slot);
    if (std::filesystem::is_regular_file(p, ec)) {
        std::ifstream f(p);
        for (std::string line; std::getline(f, line);) {
            if (line.empty()) continue;
            const size_t tab = line.find('\t');
            const std::string path = line.substr(0, tab);
            int n = tab == std::string::npos ? 0 : std::atoi(line.c_str() + tab + 1);
            if (n <= 0) n = std::max(1, CountOf(path));   // a list from before amounts: what the host has
            g_ledger[path] = std::max(g_ledger[path], n);
        }
        QD_INFO(kTag, "shared-items list of slot {}: {} items", slot, g_ledger.size());
        MergeCorrections(slot);
    }
    // whatever the host has now belongs to the agreement too (things found solo, or before the list kept them): a
    // friend who joins gets everything shared the host owns
    if (SDK::UFS_SaveSlot_C* save = ActiveSave()) {
        for (auto& kv : save->InventoryContents) {
            SDK::UFS_Item* raw = kv.Key();
            if (!raw || kv.Value() <= 0 || !ue::IsValid(raw) || !raw->IsA(SDK::UFS_ItemData_C::StaticClass())) continue;
            auto* it = static_cast<SDK::UFS_ItemData_C*>(raw);
            const std::string path = ue::PathName(it);
            if (Shared(it, path) && ue::Name(it) != "NullWeapon") g_ledger[path] = std::max(g_ledger[path], kv.Value());
        }
    }
    LedgerSave();
    QD_INFO(kTag, "shared-items list of slot {} with the host's items: {}", slot, g_ledger.size());
    return true;
}

// The agreed amount of `path` is at least `n` (default: what the host has now).
void LedgerAdd(const std::string& path, int n = -1) {
    if (!LedgerReady()) return;
    if (n < 0) n = CountOf(path);
    int& cur = g_ledger[path];
    if (n <= cur && cur > 0) return;
    cur = std::max({cur, n, 1});
    LedgerSave();
}

// A stack shown in the key menu (door keys and the like) is used up in the world: it is never topped up. Gear kept
// in the key menu (the knife belt, the scientist's weapon upgrades) is always shared.
bool TopUpAllowed(const SDK::UFS_ItemData_C* item) {
    if (!(item->ShowInKeyMenu && !item->Unstackable)) return true;
    const std::string path = ue::PathName(item);
    return path.find("KnifeBelt") != std::string::npos || path.find("WeaponUpgradeUnlock") != std::string::npos;
}

// Host: its own inventory up to the agreed amounts.
void TopUpSelf() {
    for (const auto& [path, n] : g_ledger) {
        SDK::UObject* obj = ue::LoadObject(path);
        if (!obj || !obj->IsA(SDK::UFS_ItemData_C::StaticClass())) continue;
        auto* item = static_cast<SDK::UFS_ItemData_C*>(obj);
        if (!Shared(item, path) || !TopUpAllowed(item)) continue;
        const int have = CountOf(path);
        const int give = OneOfAKind(item) ? (have > 0 ? 0 : 1) : n - have;
        if (give > 0 && GiveItem(path, give)) QD_INFO(kTag, "catch-up: host brought up to {} x{} (+{})", path, n, give);
    }
}

// Host: send every listed item to one player (they keep only what they lack).
// arg 3: "bring yours up to these amounts" (host -> friend, who answers with arg 4: its own amounts of the same items,
// and the host brings its own up to those). Either side ends with the higher amount of every shared item.
void SendAmounts(uint8_t toSlot, int32_t arg, const std::vector<std::string>& paths) {
    net::MsgParty m;
    m.kind = net::PartyKind::Gift;
    m.toSlot = toSlot;
    m.arg = arg;
    for (const auto& path : paths) {
        m.items.push_back({path, std::clamp(CountOf(path), 0, 9999)});
        if (m.items.size() == net::kMaxPartyItems) {
            Send(m);
            m.items.clear();
        }
    }
    if (!m.items.empty()) Send(m);
}

void SendCatchUp(uint8_t toSlot) {
    if (!LedgerReady() || g_ledger.empty()) return;
    TopUpSelf();
    net::MsgParty m;
    m.kind = net::PartyKind::Gift;
    m.toSlot = toSlot;
    m.arg = 3;
    for (const auto& [path, n] : g_ledger) {
        m.items.push_back({path, std::clamp(n, 0, 9999)});
        if (m.items.size() == net::kMaxPartyItems) {
            Send(m);
            m.items.clear();
        }
    }
    if (!m.items.empty()) Send(m);
    QD_INFO(kTag, "catch-up: sent the agreed amounts of {} shared items to slot {}", g_ledger.size(), toSlot);
}

bool g_catchUpAsked = false;
SDK::UObject* g_catchUpChar = nullptr;
double g_catchUpCharSince = 0;

// Client: a few seconds into a world, ask the host for the shared items this player may have missed.
void TickCatchUp(double now) {
    SDK::AFS_PlayerChar_C* p = LocalPlayer();
    if (!coop::InSession() || coop::IsHost() || !p) {
        g_catchUpChar = nullptr;
        g_catchUpAsked = false;
        return;
    }
    if (p != g_catchUpChar) {
        g_catchUpChar = p;
        g_catchUpCharSince = now;
        g_catchUpAsked = false;
    }
    if (g_catchUpAsked || now - g_catchUpCharSince < 6.0 || GameBusy(p)) return;
    g_catchUpAsked = true;
    net::MsgParty m;
    m.kind = net::PartyKind::Gift;
    m.toSlot = 0;
    m.arg = 1;   // "send me the list"
    Send(m);
}

// 1.6.11, host: its game was saved (autosave, a checkpoint's Save, the game's own): friends save their character too
void OnGameSaved(hooks::CallInfo&) {
    if (!coop::InSession() || !coop::IsHost()) return;
    net::MsgParty m;
    m.kind = net::PartyKind::Saved;
    Send(std::move(m));
}

void OnItemAdded(hooks::CallInfo& ci) {
    if (g_giving || !ci.params || !coop::InSession() || WorldSyncReplaying() || BossSyncReplaying()) return;
    SDK::ACharacter* ch = ue::LocalCharacter();
    if (!ch) return;
    const uint64_t now = util::NowMs();
    if (ch != g_lastChar) {
        g_lastChar = ch;
        g_charSince = now;
    }
    if (now - g_charSince < kSettleMs) return;
    auto* p = static_cast<SDK::Params::SaveFunctions_C_AddItemToInventory*>(ci.params);
    const std::string path = ue::ToString(p->Item.ObjectID.AssetPathName);
    if (path.empty() || p->Count <= 0 || path.size() > net::kMaxItemPathBytes) return;
    SDK::UObject* obj = ue::LoadObject(path);
    if (!obj || !obj->IsA(SDK::UFS_ItemData_C::StaticClass()) || !Shared(static_cast<SDK::UFS_ItemData_C*>(obj), path)) return;
    const int count = OneOfAKind(static_cast<SDK::UFS_ItemData_C*>(obj)) ? 1 : std::clamp(p->Count, 1, 9999);
    // 1.6.11: a pickup someone already took (still on the ground here for a moment): no second copy
    if (g_pickupSince > 0 && WorldSyncPickupTaken(g_pickupOwner)) {
        QD_INFO(kTag, "{} came from a pickup someone already took: given back", path);
        const std::string taken = path;
        const int n = p->Count;
        gamethread::Post([taken, n] {
            g_giving = true;
            TakeItem(taken, n);
            g_giving = false;
        });
        ui::Toast(ui::Tr("party.already_taken"), ui::ToastKind::Info, 3.0f);
        return;
    }
    if (ShopOpen()) {
        QD_INFO(kTag, "item {} x{} bought (shop open): not shared", path, p->Count);
        return;
    }
    LedgerAdd(path);
    NoteBigMoment();
    net::MsgParty m;
    m.kind = net::PartyKind::Gift;
    m.toSlot = net::kPartyAll;
    m.items.push_back({path, count});
    Send(m);
    QD_INFO(kTag, "shared {} x{} with the party", path, p->Count);
}

void ReceiveGift(const net::MsgParty& m) {
    if (m.arg == 1) {   // a friend asks for the shared items they may have missed
        if (coop::IsHost()) SendCatchUp(m.fromSlot);
        return;
    }
    const bool topUp = m.arg == 3 || m.arg == 4;
    int got = 0;
    const bool many = m.items.size() > 3;
    std::vector<std::string> asked;
    for (const auto& it : m.items) {
        if (m.arg == 3) asked.push_back(it.path);
        SDK::UObject* obj = ue::LoadObject(it.path);
        if (!obj || !obj->IsA(SDK::UFS_ItemData_C::StaticClass())) {
            QD_WARN(kTag, "shared item {} not found", it.path);
            continue;
        }
        auto* item = static_cast<SDK::UFS_ItemData_C*>(obj);
        if (!Shared(item, it.path)) continue;   // consumables and coins are traded, never shared
        const bool unique = OneOfAKind(item);
        if (coop::IsHost()) LedgerAdd(it.path, m.arg == 4 ? it.count : -1);   // the agreement only grows
        if (topUp && !TopUpAllowed(item)) continue;
        if (m.arg == 4) continue;   // the host: TopUpSelf below brings it up to the agreed amounts
        const int have = CountOf(it.path);
        int count;
        if (unique) count = have > 0 || (topUp && it.count <= 0) ? 0 : 1;
        else if (topUp) count = std::max(0, std::min(it.count, 9999) - have);   // up to the other's amount
        else count = std::clamp(it.count, 1, 9999);
        if (count <= 0) continue;
        if (GiveItem(it.path, count)) {
            QD_INFO(kTag, "got shared item {} x{} from {}", it.path, count, NameOf(m.fromSlot));
            ++got;
            if (!topUp) NoteBigMoment();
            if (!many) ui::Toast(ui::TrF("party.gift", ShortItemName(it.path), NameOf(m.fromSlot)), ui::ToastKind::Info, 3.0f);
        } else {
            QD_WARN(kTag, "shared item {} x{} could not be added", it.path, count);
        }
    }
    if (many && got > 0) ui::Toast(ui::TrF("party.gift_many", got), ui::ToastKind::Info, 4.0f);
    // the friend's side of the catch-up: tell the host its own amounts, so the host is brought up too
    if (m.arg == 3 && !coop::IsHost() && !asked.empty()) SendAmounts(0, 4, asked);
    if (m.arg == 4 && coop::IsHost()) TopUpSelf();
}

void OnParty(uint8_t dispatchSlot, const uint8_t* data, size_t size) {
    net::MsgParty m;
    if (!net::Decode(data, size, m)) return;
    auto local = coop::LocalSlot();
    if (!local) return;
    if (coop::IsHost()) {
        if (dispatchSlot >= net::kMaxPlayers || dispatchSlot == *local) return;
        m.fromSlot = dispatchSlot;   // never trust the slot a client claims
        const auto bytes = net::Encode(m);
        for (const auto& mem : coop::Members()) {
            if (!mem.present || mem.isLocal || mem.slot == dispatchSlot) continue;
            if (m.toSlot == net::kPartyAll || m.toSlot == mem.slot) coop::SendToSlot(mem.slot, net::Channel::Reliable, bytes);
        }
        if (m.toSlot != net::kPartyAll && m.toSlot != *local) return;
    } else if (m.toSlot != net::kPartyAll && m.toSlot != *local) {
        return;
    }
    if (m.fromSlot == *local) return;
    if (m.kind == net::PartyKind::Gift) return ReceiveGift(m);

    auto& s = S();
    switch (m.kind) {
        case net::PartyKind::Downed:
            s.downed[m.fromSlot] = m.arg != 0;
            if (m.arg == 1) ui::Toast(ui::TrF("party.mate_down", NameOf(m.fromSlot)), ui::ToastKind::Warning, 5.0f);
            if (m.arg == 2) ui::Toast(ui::TrF("party.mate_gave_up", NameOf(m.fromSlot)), ui::ToastKind::Warning, 8.0f);
            if (m.arg == 0 && s.reviveTarget == m.fromSlot) StopReviving();
            return;
        case net::PartyKind::Reviving:
            if (!s.localDowned) return;
            if (m.arg == 1) {
                s.revivingMe = static_cast<int8_t>(m.fromSlot);
                g_revivingSince = Now();
                ui::Toast(ui::TrF("party.being_revived", NameOf(m.fromSlot)), ui::ToastKind::Info, 3.0f);
            } else if (s.revivingMe == m.fromSlot) {
                s.revivingMe = -1;
            }
            return;
        case net::PartyKind::Revive:
            StandUp(m.fromSlot);
            return;
        case net::PartyKind::Rest:
            if (s.rest.step != RestStep::Idle) return;   // one at a time
            StartRest(m);
            return;
        case net::PartyKind::Saved:
            if (m.fromSlot == 0) CharacterHostSaved();
            return;
        default:
            OnTradeMsg(m);
            return;
    }
}

void OnMember(const coop::Member& m, bool joined) {
    auto& s = S();
    if (m.slot < net::kMaxPlayers) s.downed[m.slot] = false;
    if (joined && s.localDowned) {   // tell the newcomer we are down
        net::MsgParty d;
        d.kind = net::PartyKind::Downed;
        d.arg = 1;
        d.toSlot = coop::IsHost() ? m.slot : net::kPartyAll;
        Send(std::move(d));
    }
    if (!joined && s.reviveTarget == m.slot) s.reviveTarget = -1;
}

// ------------------------------------------------------------------ autosave (1.6.10)
// A game over reloads the host's last save, while a friend's character file is saved every two minutes: play test,
// the host lost a boss upgrade, loot sacks and the double dash the friend kept, and the pickups stayed gone from the
// world. The host's game is now saved every two minutes and right after a big moment (an upgrade, key item, weapon
// or loot sack is picked up or shared), as the host picks in F5 > Co-op (default: both).
constexpr double kAutosaveEverySec = 120.0;
constexpr double kMomentDelaySec   = 3.0;    // after the pickup's own animation
double g_nextAutosave = 0, g_momentAt = -1;

bool SafeToSave(SDK::AFS_PlayerChar_C* p) {
    auto& s = S();
    if (!p || p->IsDead || p->Teleporting || p->QuickTeleporting || p->Resting || GameBusy(p)) return false;
    if (s.localDowned || s.rest.step != RestStep::Idle || g_pickupSince > 0) return false;
    for (const auto& m : coop::Members())
        if (m.present && s.downed[m.slot]) return false;
    return true;
}

void NoteBigMoment() {
    if (coop::IsHost() && config::Get().autosave != "off" && g_momentAt < 0) g_momentAt = Now() + kMomentDelaySec;
}

void TickAutosave(double now) {
    if (!coop::InSession() || !coop::IsHost()) {
        g_nextAutosave = now + kAutosaveEverySec;
        g_momentAt = -1;
        return;
    }
    const std::string& mode = config::Get().autosave;
    if (mode == "off") return;
    const bool moment = g_momentAt >= 0 && now >= g_momentAt;
    const bool timer = mode == "both" && now >= g_nextAutosave;
    if (!moment && !timer) return;
    SDK::AFS_PlayerChar_C* p = LocalPlayer();
    if (!SafeToSave(p)) return;   // tried again next tick
    SDK::UObject* cdo = SaveFunctions();
    if (!cdo) return;
    SDK::Params::SaveFunctions_C_SaveGameToDisk sp{};
    sp.__WorldContext = p;
    if (Call(cdo, "SaveGameToDisk", sp)) QD_INFO(kTag, "autosave ({})", moment ? "big moment" : "every two minutes");
    g_nextAutosave = now + kAutosaveEverySec;
    g_momentAt = -1;
}

// ------------------------------------------------------------------ tick + UI

void Tick(float) {
    auto& s = S();
    const double now = Now();
    TickRevive(now);
    TickRest(now);
    TickTrade(now);
    TickTradePrompt(now);
    TickWatchdogs(now);
    TickCatchUp(now);
    TickAutosave(now);
    ui::SetInputCapture("trade", s.trade.stage == TradeStage::Open);

    if (now - s.invAt > 0.5) {
        s.invAt = now;
        s.inv = (ue::IsValid(ue::LocalCharacter()) && coop::InSession()) ? ReadInventory() : std::vector<InvLine>{};
    }
    UiSnap snap;
    snap.inSession = coop::InSession();
    snap.inGame = LocalPlayer() != nullptr;
    snap.localDowned = s.localDowned;
    for (const auto& m : coop::Members()) {
        if (!m.present || m.isLocal) continue;
        snap.mates.push_back({m.slot, NameOf(m.slot), s.downed[m.slot], snap.inGame && TeammateNear(m.slot, kTradeRangeCm)});
    }
    snap.trade = s.trade;
    if (s.trade.peer < net::kMaxPlayers) snap.peerName = NameOf(s.trade.peer);
    snap.inv = s.inv;
    TickDownedView(now);
    BuildPrompt(snap, now);
    std::lock_guard lk(s.uiMu);
    s.ui = std::move(snap);
}

const ImVec4 kColOk(0.55f, 0.86f, 0.62f, 1.0f);
const ImVec4 kColWarn(0.96f, 0.77f, 0.38f, 1.0f);

void DrawTradeBody(const UiSnap& s);

void DrawTab() {
    UiSnap s;
    {
        std::lock_guard lk(S().uiMu);
        s = S().ui;
    }
    using ui::Tr;
    if (!s.inSession) {
        ImGui::TextWrapped("%s", Tr("party.not_in_session"));
        return;
    }
    ImGui::TextWrapped("%s", Tr("party.help"));
    ImGui::Separator();

    const Trade& t = s.trade;
    if (t.stage == TradeStage::None) {
        ImGui::TextUnformatted(Tr("party.mates"));
        for (const auto& m : s.mates) {
            ImGui::PushID(m.slot);
            ImGui::Text("%s", m.name.c_str());
            if (m.downed) {
                ImGui::SameLine();
                ImGui::TextColored(kColWarn, "%s", Tr("party.state_down"));
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(!m.near || m.downed || s.localDowned);
            if (ImGui::Button(Tr("trade.button"))) {
                const uint8_t slot = m.slot;
                gamethread::Post([slot] { TradeRequest(slot); });
            }
            ImGui::EndDisabled();
            if (!m.near) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", Tr("trade.come_closer"));
            }
            ImGui::PopID();
        }
        if (s.mates.empty()) ImGui::TextDisabled("%s", Tr("party.no_mates"));
        return;
    }

    DrawTradeBody(s);
}

void DrawTradeBody(const UiSnap& s) {
    using ui::Tr;
    const Trade& t = s.trade;
    const std::string& peer = s.peerName;
    if (t.stage == TradeStage::Outgoing) {
        ImGui::TextWrapped("%s", ui::TrF("trade.waiting", peer).c_str());
        if (ImGui::Button(Tr("trade.cancel"))) gamethread::Post([] { TradeCancel(); });
        return;
    }
    if (t.stage == TradeStage::Incoming) {
        ImGui::TextWrapped("%s", ui::TrF("trade.incoming", peer).c_str());
        if (ImGui::Button(Tr("trade.accept"))) gamethread::Post([] { TradeAnswer(true); });
        ImGui::SameLine();
        if (ImGui::Button(Tr("trade.decline"))) gamethread::Post([] { TradeAnswer(false); });
        return;
    }

    // open trade: inventory-style tiles. Yours: left click offers one more, right click takes one back.
    auto tile = [](const char* id, const std::string& name, const std::string& sub, bool lit) -> int {
        const ImVec2 size(150.0f, 72.0f);
        ImGui::PushID(id);
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##tile", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        int click = 0;
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) click = 1;
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) click = -1;
        const bool hover = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p1(p0.x + size.x, p0.y + size.y);
        dl->AddRectFilled(p0, p1, hover ? IM_COL32(52, 38, 24, 255) : IM_COL32(30, 22, 16, 255));
        dl->AddRect(p0, p1, lit ? IM_COL32(235, 195, 95, 255) : IM_COL32(120, 95, 55, 255), 0.0f, 0, lit ? 2.5f : 1.0f);
        dl->PushClipRect(p0, p1, true);
        dl->AddText(nullptr, 0.0f, ImVec2(p0.x + 8.0f, p0.y + 7.0f), IM_COL32(245, 234, 210, 255), name.c_str(), nullptr, size.x - 14.0f);
        dl->AddText(ImVec2(p0.x + 8.0f, p1.y - 22.0f), lit ? IM_COL32(235, 195, 95, 255) : IM_COL32(170, 150, 120, 255), sub.c_str());
        dl->PopClipRect();
        ImGui::PopID();
        return click;
    };
    auto flow = [](int i, int perRow) { if (i % perRow != 0) ImGui::SameLine(); };
    ImGui::TextUnformatted(Tr("trade.your_offer"));
    ImGui::TextDisabled("%s", Tr("trade.tile_help"));
    if (s.inv.empty()) ImGui::TextDisabled("%s", Tr("trade.nothing"));
    ImGui::BeginDisabled(t.committing);
    int i = 0;
    for (const auto& l : s.inv) {
        auto it = t.mine.find(l.path);
        const int cur = it == t.mine.end() ? 0 : it->second;
        flow(i++, 4);
        const std::string sub = cur > 0 ? std::to_string(cur) + " / " + std::to_string(l.have) : "x" + std::to_string(l.have);
        const int c = tile(l.path.c_str(), l.name, sub, cur > 0);
        const int step = ImGui::GetIO().KeyShift ? 10 : 1;
        if (c != 0) {
            const std::string path = l.path;
            const int want = cur + c * step;
            gamethread::Post([path, want] { TradeSetCount(path, want); });
        }
    }
    ImGui::EndDisabled();
    ImGui::Spacing();
    ImGui::TextUnformatted(ui::TrF("trade.their_offer", peer).c_str());
    if (t.theirs.empty()) ImGui::TextDisabled("%s", Tr("trade.nothing"));
    i = 0;
    for (const auto& l : t.theirs) {
        flow(i++, 4);
        tile(("their" + l.path).c_str(), l.name, "x" + std::to_string(l.count), true);
    }
    ImGui::Separator();
    ImGui::Text("%s %s   %s %s", Tr("trade.you"), t.myConfirm ? Tr("trade.confirmed") : Tr("trade.not_confirmed"),
                peer.c_str(), t.theirConfirm ? Tr("trade.confirmed") : Tr("trade.not_confirmed"));
    ImGui::BeginDisabled(t.committing);
    if (!t.myConfirm) {
        if (ImGui::Button(Tr("trade.confirm"))) gamethread::Post([] { TradeConfirm(true); });
    } else if (ImGui::Button(Tr("trade.unconfirm"))) {
        gamethread::Post([] { TradeConfirm(false); });
    }
    ImGui::SameLine();
    if (ImGui::Button(Tr("trade.cancel"))) gamethread::Post([] { TradeCancel(); });
    ImGui::EndDisabled();
}

// The trade window, in the game's colours: dark panel, gold frame, cream text. Open trades only; a request shows as a
// line at the bottom of the screen (no input taken, so nobody is stopped mid-fight).
void DrawTradeHud() {
    UiSnap s;
    {
        std::lock_guard lk(S().uiMu);
        s = S().ui;
    }
    ImGuiIO& io = ImGui::GetIO();
    if (s.inSession && !s.prompt.empty() && s.trade.stage != TradeStage::Open) {
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        ImFont* font = ImGui::GetFont();
        const float fs = ImGui::GetFontSize() * 1.25f;
        const ImVec2 sz = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, s.prompt.c_str());
        const float cx = io.DisplaySize.x * 0.5f, y = io.DisplaySize.y * 0.60f;
        const float w = std::max(sz.x + 40.0f, 280.0f);
        const float h = sz.y + (s.progress >= 0 ? 34.0f : 18.0f);
        const ImVec2 p0(cx - w * 0.5f, y), p1(cx + w * 0.5f, y + h);
        dl->AddRectFilled(p0, p1, IM_COL32(18, 13, 22, 210));
        dl->AddRect(p0, p1, IM_COL32(217, 180, 90, 255), 0.0f, 0, 2.0f);
        dl->AddText(font, fs, ImVec2(cx - sz.x * 0.5f + 1, y + 10.0f), IM_COL32(0, 0, 0, 200), s.prompt.c_str());
        dl->AddText(font, fs, ImVec2(cx - sz.x * 0.5f, y + 9.0f), IM_COL32(245, 234, 210, 255), s.prompt.c_str());
        if (s.progress >= 0) {
            const ImVec2 b0(p0.x + 16.0f, p1.y - 18.0f), b1(p1.x - 16.0f, p1.y - 10.0f);
            dl->AddRectFilled(b0, b1, IM_COL32(60, 46, 30, 255));
            dl->AddRectFilled(b0, ImVec2(b0.x + (b1.x - b0.x) * std::clamp(s.progress, 0.0f, 1.0f), b1.y), IM_COL32(230, 190, 90, 255));
        }
    }
    const Trade& t = s.trade;
    if (!s.inSession || t.stage == TradeStage::None) return;
    if (t.stage != TradeStage::Open) return;   // a request is a short note, not a line kept on the screen
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.07f, 0.05f, 0.09f, 0.97f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.85f, 0.71f, 0.35f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.96f, 0.92f, 0.82f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.24f, 0.17f, 0.10f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.45f, 0.32f, 0.14f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.62f, 0.47f, 0.18f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TableBorderLight, ImVec4(0.85f, 0.71f, 0.35f, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.85f, 0.71f, 0.35f, 0.6f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22.0f, 18.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 8.0f));
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(560.0f, 0.0f), ImVec2(io.DisplaySize.x * 0.8f, io.DisplaySize.y * 0.8f));
    g_tradeDrawnAt = Now();
    if (ImGui::Begin("##qd_trade", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::SetWindowFontScale(1.35f);
        ImGui::TextColored(ImVec4(0.92f, 0.78f, 0.40f, 1.0f), "%s", ui::TrF("trade.title", s.peerName).c_str());
        ImGui::SetWindowFontScale(1.1f);
        ImGui::Separator();
        DrawTradeBody(s);
        ImGui::Spacing();
        ImGui::TextDisabled("%s", ui::Tr("trade.esc_hint"));
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) gamethread::Post([] { TradeCancel(); });
        ImGui::SetWindowFontScale(1.0f);
    }
    ImGui::End();
    ImGui::PopStyleVar(5);
    ImGui::PopStyleColor(8);
}

}  // namespace

// 1.6.10, F5 > Fix: the host gets back every shared item the party agreed on and it lacks (a game over or a death
// reload took it), also kinds the join catch-up leaves alone (loot sacks, upgrades, key items).
void PartyRestoreItems() {
    gamethread::Post([] {
        if (!coop::InSession() || !coop::IsHost() || !LedgerReady()) {
            ui::Toast(ui::Tr("fix.restore_none"), ui::ToastKind::Info, 4.0f);
            return;
        }
        int got = 0;
        for (const auto& [path, n] : g_ledger) {
            SDK::UObject* obj = ue::LoadObject(path);
            if (!obj || !obj->IsA(SDK::UFS_ItemData_C::StaticClass())) continue;
            auto* item = static_cast<SDK::UFS_ItemData_C*>(obj);
            if (!Shared(item, path)) continue;
            const int have = CountOf(path);
            const int give = OneOfAKind(item) ? (have > 0 ? 0 : 1) : n - have;
            if (give > 0 && GiveItem(path, give)) {
                QD_INFO(kTag, "restore: {} x{} (+{})", path, n, give);
                ++got;
            }
        }
        if (got) NoteBigMoment();
        ui::Toast(got ? ui::TrF("fix.restore_done", got) : ui::Tr("fix.restore_none"), ui::ToastKind::Success, 5.0f);
    });
}

void PartyUnstuck() {
    gamethread::Post([] {
        SDK::AFS_PlayerChar_C* p = LocalPlayer();
        if (!p) return;
        QD_INFO(kTag, "unstuck (Fix tab): teleporting {} quick {}, pick-up pause {}, input lock {}", p->Teleporting, p->QuickTeleporting,
                g_pickupSince > 0, g_inputLocked);
        S().rest = RestJob{};
        S().driving = false;
        // every way the game takes the controls away, given back: a teleport, loading or a cutscene, a pick-up
        // pause, ignored move or look input, paused time
        EndStuckTeleport(p);
        EndStuckPickup(p);
        g_inputLocked = false;
        g_lockedSince = g_stuckSince = -1;
        p->DisableJump = false;
        p->KnockedDown = false;
        if (p->CharacterMovement && ue::IsValid(p->CharacterMovement))
            p->CharacterMovement->SetMovementMode(SDK::EMovementMode::MOVE_Falling, 0);
        ui::SetInputCapture("trade", false);
        ui::SetInputCapture("lobby", false);
    });
}

void PartyForceStandUp() {
    gamethread::Post([] {
        auto slot = coop::LocalSlot();
        if (!S().localDowned || !slot) return;
        QD_INFO(kTag, "stand up (Fix tab)");
        StandUp(*slot);
    });
}

void PartyHostNewGame(int slot) {
    std::error_code ec;
    std::filesystem::remove(LedgerPath(slot), ec);
    if (g_ledgerSlot == slot) g_ledgerSlot = -1000;
}

bool PartyLocalDowned() { return S().localDowned; }

bool PartyPickingUp(const SDK::UObject* pickup) { return pickup && g_pickupSince > 0 && pickup == g_pickupOwner; }
bool PartySlotDowned(uint8_t slot) { return slot < net::kMaxPlayers && S().downed[slot]; }

std::string PartyLabelSuffix(uint8_t slot) {
    auto& s = S();
    if (slot >= net::kMaxPlayers) return {};
    if (!s.downed[slot]) return {};
    return ui::Tr("party.label_down");
}

#if QD_DEV
void PartyDebug(const std::string& args) {
    auto& s = S();
    std::istringstream in(args);
    std::string cmd, sub;
    in >> cmd;
    if (cmd == "down") { int v = 1; in >> v; s.downEnabled = v != 0; }
    else if (cmd == "hold") { int v = 1; in >> v; s.forceHold = v != 0; }
    else if (cmd == "trade") {
        in >> sub;
        if (sub == "req") { int sl = 1; in >> sl; TradeRequest(static_cast<uint8_t>(sl)); }
        else if (sub == "accept") TradeAnswer(true);
        else if (sub == "decline") TradeAnswer(false);
        else if (sub == "offer") { std::string path; int n = 1; in >> path >> n; TradeSetCount(path, n); }
        else if (sub == "confirm") { int v = 1; in >> v; TradeConfirm(v != 0); }
        else if (sub == "cancel") TradeCancel();
    }
    std::string downed;
    for (int i = 0; i < net::kMaxPlayers; ++i) downed += (i ? "," : "") + std::string(s.downed[i] ? "true" : "false");
    std::string inv;
    for (const auto& l : ReadInventory()) {
        std::string esc;
        for (char c : l.path) esc += (c == '"' || c == 0x5C) ? '_' : c;
        inv += (inv.empty() ? "" : ",") + std::format("\"{}\":{}", esc, l.have);
    }
    SDK::AFS_PlayerChar_C* p = LocalPlayer();
    QD_INFO(kTag, "party state {{\"downEnabled\":{},\"localDowned\":{},\"downed\":[{}],\"reviveTarget\":{},"
                  "\"reviveCandidate\":{},\"progress\":{:.2f},\"rest\":{},\"driving\":{},\"trade\":{},"
                  "\"tradePeer\":{},\"myConfirm\":{},\"theirConfirm\":{},\"theirs\":{},\"health\":{:.1f},"
                  "\"dead\":{},\"inv\":{{{}}}}}",
            s.downEnabled, s.localDowned, downed, s.reviveTarget, s.reviveCandidate, s.reviveProgress,
            static_cast<int>(s.rest.step), s.driving, static_cast<int>(s.trade.stage), static_cast<int>(s.trade.peer),
            s.trade.myConfirm, s.trade.theirConfirm, s.trade.theirs.size(), p ? p->Health : -1.0f,
            p ? p->IsDead : false, inv);
}
#endif

void PartyInit() {
    auto& s = S();
    if (s.inited) return;
    s.inited = true;
    coop::On(net::MsgType::Party, OnParty);
    coop::AddMemberListener(OnMember);
    s.hooks.push_back(hooks::AddPre(kFnKill, OnKillPre));
    s.hooks.push_back(hooks::AddPre(kFnPlayerDamage, OnPlayerDamagePre));
    for (const char* fn : kBlockedWhileDown)
        s.hooks.push_back(hooks::AddPre((std::string(kPlayerClass) + ":" + fn).c_str(), OnBlockedInputPre));
    s.hooks.push_back(hooks::AddPost(kFnRestButton, [](hooks::CallInfo& ci) { OnMenuButton(ci, false); }));
    s.hooks.push_back(hooks::AddPost(kFnSaveButton, [](hooks::CallInfo& ci) { OnMenuButton(ci, true); }));
    s.hooks.push_back(hooks::AddPost("/Game/Blueprints/SaveSystem/SaveFunctions.SaveFunctions_C:AddItemToInventory", OnItemAdded));
    s.hooks.push_back(hooks::AddPost("/Game/Blueprints/SaveSystem/SaveFunctions.SaveFunctions_C:SaveGameToDisk", OnGameSaved));
    s.hooks.push_back(hooks::AddPost("/Game/Blueprints/ItemPickupAnimationComp.ItemPickupAnimationComp_C:StartPickupPause", OnPickupPauseStart));
    s.hooks.push_back(hooks::AddPost("/Game/Blueprints/ItemPickupAnimationComp.ItemPickupAnimationComp_C:EndPickupPause", OnPickupPauseEnd));
    const std::string pc = kPlayerClass;
    s.hooks.push_back(hooks::AddPost((pc + ":EnterUIMode").c_str(), [](hooks::CallInfo& ci) { OnPlayerFlag(ci, &g_uiMode, true); }));
    s.hooks.push_back(hooks::AddPost((pc + ":ExitUIMode").c_str(), [](hooks::CallInfo& ci) { OnPlayerFlag(ci, &g_uiMode, false); }));
    s.hooks.push_back(hooks::AddPost((pc + ":StartDialogue").c_str(), [](hooks::CallInfo& ci) { OnPlayerFlag(ci, &g_dialogue, true); }));
    s.hooks.push_back(hooks::AddPost((pc + ":DialogueEnded").c_str(), [](hooks::CallInfo& ci) { OnPlayerFlag(ci, &g_dialogue, false); }));
    s.hooks.push_back(hooks::AddPost((pc + ":LockInputForLoadingOrCutscene").c_str(), [](hooks::CallInfo& ci) { OnPlayerFlag(ci, &g_inputLocked, true); }));
    s.hooks.push_back(hooks::AddPost((pc + ":UnlockInputForLoadingOrCutscene").c_str(), [](hooks::CallInfo& ci) { OnPlayerFlag(ci, &g_inputLocked, false); }));
    s.ticker = gamethread::AddTicker("party", Tick);
    ui::AddHudDrawer("trade", DrawTradeHud);
    QD_INFO(kTag, "party features ready (revive, party rest, trading)");
}

void PartyShutdown() {
    auto& s = S();
    if (!s.inited) return;
    for (auto id : s.hooks) hooks::Remove(id);
    s.hooks.clear();
    if (s.ticker) gamethread::RemoveTicker(s.ticker);
    s.ticker = 0;
    s.inited = false;
}

}  // namespace qd::sync
