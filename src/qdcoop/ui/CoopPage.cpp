// Knights' Domain: the co-op page of the main menu (see CoopPage.h).
//
// Built like the Co-op button (TitleButton.cpp): FS_Button_C widgets created for the title screen's player, each with a
// CommonTextBlock label styled like Play Game's, added to the menu's VerticalBox with Play Game's slot settings. The
// menu's own entries are collapsed while the page is open and shown again by Back. Faces are UImages showing the
// knight's face textures (Knight_Face_0N_TX) inside buttons. The page is rebuilt when what it shows changes.
//
// Include order: SDK headers FIRST (see GameQueries.cpp for why).
#include "SDK/Engine_classes.hpp"
#include "SDK/Engine_parameters.hpp"
#include "SDK/UMG_classes.hpp"
#include "SDK/UMG_parameters.hpp"
#include "SDK/CommonUI_classes.hpp"
#include "SDK/CommonUI_parameters.hpp"
#include "SDK/TitleScreenUI_classes.hpp"
#include "SDK/FS_Button_classes.hpp"
#include "SDK/Slate_structs.hpp"

#include "qdcoop/ui/CoopPage.h"

#include "qdcoop/core/Config.h"
#include "qdcoop/core/Core.h"
#include "qdcoop/core/Hooks.h"
#include "qdcoop/core/Log.h"
#include "qdcoop/core/Unreal.h"
#include "qdcoop/net/Protocol.h"
#include "qdcoop/sync/Coop.h"
#include "qdcoop/sync/CoopInternal.h"
#include "qdcoop/sync/EnemySync.h"
#include "qdcoop/sync/Lobby.h"
#include "qdcoop/sync/Look.h"
#include "qdcoop/ui/I18n.h"
#include "qdcoop/ui/Overlay.h"

#include <Windows.h>

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace qd::ui {
namespace {

constexpr const char* kTag = "coop-page";
constexpr const char* kButtonClass = "/Game/UI/UI_Style/FS_Button.FS_Button_C";
constexpr const char* kButtonClicked = "/Game/UI/UI_Style/FS_Button.FS_Button_C:BP_OnClicked";
constexpr float kRefreshSec = 0.4f;
constexpr float kLobbyListSec = 5.0f;
constexpr float kFaceSize = 52.0f;

struct Page {
    bool inited = false;
    bool open = false;
    SDK::UTitleScreenUI_C* title = nullptr;
    SDK::UPanelWidget* box = nullptr;
    // Play Game's slot in the menu column: every item of the page is added with these
    SDK::FSlateChildSize size{};
    SDK::FMargin pad{};
    SDK::EHorizontalAlignment h{};
    SDK::EVerticalAlignment v{};
    SDK::UCommonTextBlock* model = nullptr;   // Play Game's label: the text style of the page
    std::vector<SDK::UWidget*> hidden;        // the menu's own entries, collapsed while the page is open
    SDK::UWidget* logo = nullptr;             // the Queen's Domain logo, hidden while the page is open
    SDK::ESlateVisibility logoWas = SDK::ESlateVisibility::Visible;
    std::vector<SDK::UWidget*> added;         // the page's items
    SDK::UBorder* frame = nullptr;            // gold edge, in the menu column
    SDK::UVerticalBox* inner = nullptr;       // the page's own column, on a dark panel inside the frame
    SDK::UUserWidget* layer = nullptr;        // the page's own screen layer (centred, above the menu)
    std::unordered_map<const SDK::UObject*, std::function<void()>> actions;
    SDK::UEditableTextBox* nameBox = nullptr;
    std::string shown;                        // what the page shows (rebuilt when it changes)
    float refreshIn = 0, lobbiesIn = 0;
};
Page& P() {
    static Page p;
    return p;
}

template <class T>
bool Call(SDK::UObject* obj, const char* fn, T& params) {
    SDK::UFunction* f = obj ? ue::FindFunction(obj, fn) : nullptr;
    if (!f) {
        QD_WARN(kTag, "{} not found on {}", fn, obj ? ue::ClassName(obj) : std::string("null"));
        return false;
    }
    ue::CallFunction(obj, f, &params);
    return true;
}

std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

SDK::FText ToText(const std::string& utf8) {
    const std::wstring w = Widen(utf8);
    return SDK::UKismetTextLibrary::Conv_StringToText(SDK::FString(w.c_str()));
}

SDK::UCommonTextBlock* PlayLabel(SDK::UObject* title) {
    SDK::UClass* cls = SDK::UCommonTextBlock::StaticClass();
    auto& objs = SDK::UObject::GObjects;
    for (int i = 0; i < objs->Num(); ++i) {
        SDK::UObject* o = objs->GetByIndex(i);
        if (!o || o->IsDefaultObject() || !o->IsA(cls) || ue::Name(o) != "UseButtontext_2") continue;
        for (SDK::UObject* out = o->Outer; out; out = out->Outer)
            if (out == title) return static_cast<SDK::UCommonTextBlock*>(o);
    }
    return nullptr;
}

void CopyFont(SDK::FSlateFontInfo& f, const SDK::FSlateFontInfo& m, float scale) {
    // field by field: FSlateFontInfo also holds engine shared pointers that must not be copied
    f.FontObject = m.FontObject;
    f.FontMaterial = m.FontMaterial;
    f.OutlineSettings.OutlineSize = m.OutlineSettings.OutlineSize;
    f.OutlineSettings.bSeparateFillAlpha = m.OutlineSettings.bSeparateFillAlpha;
    f.OutlineSettings.bApplyOutlineToDropShadows = m.OutlineSettings.bApplyOutlineToDropShadows;
    f.OutlineSettings.OutlineMaterial = m.OutlineSettings.OutlineMaterial;
    f.OutlineSettings.OutlineColor = m.OutlineSettings.OutlineColor;
    f.TypefaceFontName = m.TypefaceFontName;
    f.Size = static_cast<int32_t>(std::lround(m.Size * scale));
    f.LetterSpacing = m.LetterSpacing;
    f.PixelateConfig = m.PixelateConfig;
}

// A text block styled like Play Game's label. dim: greyed (information, not a button).
SDK::UCommonTextBlock* NewText(SDK::UObject* outer, const std::string& text, float scale = 1.0f, bool dim = false,
                               const SDK::FLinearColor* color = nullptr) {
    auto& p = P();
    SDK::UObject* o = SDK::UGameplayStatics::SpawnObject(SDK::UCommonTextBlock::StaticClass(), outer);
    if (!o) return nullptr;
    auto* label = static_cast<SDK::UCommonTextBlock*>(o);
    if (SDK::UCommonTextBlock* m = p.model; m && ue::IsValid(m)) {
        label->Style = m->Style;
        SDK::Params::CommonTextBlock_SetStyle st{};
        st.InStyle = m->Style;
        Call(label, "SetStyle", st);
        label->RenderTransform = m->RenderTransform;
        label->RenderTransformPivot = m->RenderTransformPivot;
        label->bDisplayAllCaps = m->bDisplayAllCaps;
        CopyFont(label->Font, m->Font, scale);
        label->ColorAndOpacity.SpecifiedColor = m->ColorAndOpacity.SpecifiedColor;
        label->ColorAndOpacity.ColorUseRule = m->ColorAndOpacity.ColorUseRule;
        label->ShadowOffset = m->ShadowOffset;
        label->ShadowColorAndOpacity = m->ShadowColorAndOpacity;
    }
    label->Justification = SDK::ETextJustify::Center;
    if (color) {
        label->ColorAndOpacity.SpecifiedColor = *color;
        label->ColorAndOpacity.ColorUseRule = SDK::ESlateColorStylingMode::UseColor_Specified;
    }
    if (dim) {
        SDK::FLinearColor& c = label->ColorAndOpacity.SpecifiedColor;
        c.R *= 0.7f;
        c.G *= 0.7f;
        c.B *= 0.7f;
    }
    SDK::Params::TextBlock_SetText tx{};
    tx.InText = ToText(text);
    Call(label, "SetText", tx);
    return label;
}

// The game's own menu button, with `content` (a label or a picture) inside.
SDK::UFS_Button_C* NewButton(SDK::UWidget* content) {
    auto& p = P();
    SDK::UClass* cls = ue::FindClass(kButtonClass);
    if (!cls || !p.title) return nullptr;
    SDK::Params::WidgetBlueprintLibrary_Create cr{};
    cr.WorldContextObject = p.title;
    cr.WidgetType = cls;
    cr.OwningPlayer = ue::LocalController();
    if (!Call(SDK::UWidgetBlueprintLibrary::GetDefaultObj(), "Create", cr) || !cr.ReturnValue) return nullptr;
    auto* button = static_cast<SDK::UFS_Button_C*>(cr.ReturnValue);
    if (SDK::UFS_Button_C* play = p.title->LoadGameButton; play && ue::IsValid(play)) {
        button->SpecialSounds = play->SpecialSounds;
        button->Style = play->Style;
    }
    if (content && button->Subcontent) {
        SDK::Params::ContentWidget_SetContent sc{};
        sc.Content = content;
        Call(button->Subcontent, "SetContent", sc);
        struct { SDK::EHorizontalAlignment a; } fill{SDK::EHorizontalAlignment::HAlign_Fill};
        if (SDK::UPanelSlot* ns = button->Subcontent->Slot)
            if (SDK::UFunction* f = ue::FindFunction(ns, "SetHorizontalAlignment")) ue::CallFunction(ns, f, &fill);
        if (sc.ReturnValue)
            if (SDK::UFunction* f = ue::FindFunction(sc.ReturnValue, "SetHorizontalAlignment")) ue::CallFunction(sc.ReturnValue, f, &fill);
    }
    return button;
}

SDK::UFS_Button_C* TextButton(const std::string& text, std::function<void()> action) {
    auto& p = P();
    // the label is created after the button so it can live in the button's widget tree
    SDK::UFS_Button_C* b = NewButton(nullptr);
    if (!b) return nullptr;
    SDK::UObject* outer = b->WidgetTree ? static_cast<SDK::UObject*>(b->WidgetTree) : static_cast<SDK::UObject*>(b);
    if (SDK::UCommonTextBlock* label = NewText(outer, text)) {
        SDK::Params::ContentWidget_SetContent sc{};
        sc.Content = label;
        if (b->Subcontent) {
            Call(b->Subcontent, "SetContent", sc);
            struct { SDK::EHorizontalAlignment a; } fill{SDK::EHorizontalAlignment::HAlign_Fill};
            if (SDK::UPanelSlot* ns = b->Subcontent->Slot)
                if (SDK::UFunction* f = ue::FindFunction(ns, "SetHorizontalAlignment")) ue::CallFunction(ns, f, &fill);
            if (sc.ReturnValue)
                if (SDK::UFunction* f = ue::FindFunction(sc.ReturnValue, "SetHorizontalAlignment")) ue::CallFunction(sc.ReturnValue, f, &fill);
        }
    }
    p.actions[b] = std::move(action);
    return b;
}

void Add(SDK::UWidget* w, float gapAbove = 6.0f) {
    auto& p = P();
    if (!w || !p.inner) return;
    SDK::Params::VerticalBox_AddChildToVerticalBox ad{};
    ad.Content = w;
    if (!Call(p.inner, "AddChildToVerticalBox", ad) || !ad.ReturnValue) return;
    SDK::Params::VerticalBoxSlot_SetPadding sp{};
    sp.InPadding = SDK::FMargin{0.0f, gapAbove, 0.0f, 0.0f};
    Call(ad.ReturnValue, "SetPadding", sp);
    SDK::Params::VerticalBoxSlot_SetHorizontalAlignment sh{};
    sh.InHorizontalAlignment = SDK::EHorizontalAlignment::HAlign_Center;
    Call(ad.ReturnValue, "SetHorizontalAlignment", sh);
    p.added.push_back(w);
}

// The frame in the menu column: a gold edge around a dark, slightly see-through panel holding the page's column.
void MakeFrame() {
    auto& p = P();
    SDK::UObject* tree = p.title->WidgetTree ? static_cast<SDK::UObject*>(p.title->WidgetTree) : static_cast<SDK::UObject*>(p.title);
    auto* frame = static_cast<SDK::UBorder*>(SDK::UGameplayStatics::SpawnObject(SDK::UBorder::StaticClass(), tree));
    auto* panel = static_cast<SDK::UBorder*>(SDK::UGameplayStatics::SpawnObject(SDK::UBorder::StaticClass(), tree));
    auto* inner = static_cast<SDK::UVerticalBox*>(SDK::UGameplayStatics::SpawnObject(SDK::UVerticalBox::StaticClass(), tree));
    if (!frame || !panel || !inner) return;
    SDK::Params::Border_SetBrushColor gold{};
    gold.InBrushColor = SDK::FLinearColor{0.72f, 0.56f, 0.24f, 0.95f};
    Call(frame, "SetBrushColor", gold);
    SDK::Params::Border_SetPadding edge{};
    edge.InPadding = SDK::FMargin{2.0f, 2.0f, 2.0f, 2.0f};
    Call(frame, "SetPadding", edge);
    SDK::Params::Border_SetBrushColor dark{};
    dark.InBrushColor = SDK::FLinearColor{0.004f, 0.003f, 0.006f, 0.9f};
    Call(panel, "SetBrushColor", dark);
    SDK::Params::Border_SetPadding room{};
    room.InPadding = SDK::FMargin{36.0f, 18.0f, 36.0f, 22.0f};
    Call(panel, "SetPadding", room);
    SDK::Params::ContentWidget_SetContent c1{};
    c1.Content = inner;
    Call(panel, "SetContent", c1);
    SDK::Params::ContentWidget_SetContent c2{};
    c2.Content = panel;
    Call(frame, "SetContent", c2);
    // centred on the whole screen: a layer of its own (a user widget on the viewport whose root canvas is the window)
    bool placed = false;
    SDK::Params::WidgetBlueprintLibrary_Create cr{};
    cr.WorldContextObject = p.title;
    cr.WidgetType = SDK::UUserWidget::StaticClass();
    cr.OwningPlayer = ue::LocalController();
    if (Call(SDK::UWidgetBlueprintLibrary::GetDefaultObj(), "Create", cr) && cr.ReturnValue) {
        SDK::UUserWidget* layer = cr.ReturnValue;
        if (!layer->WidgetTree)
            layer->WidgetTree = static_cast<SDK::UWidgetTree*>(SDK::UGameplayStatics::SpawnObject(SDK::UWidgetTree::StaticClass(), layer));
        auto* canvas = layer->WidgetTree
                           ? static_cast<SDK::UCanvasPanel*>(SDK::UGameplayStatics::SpawnObject(SDK::UCanvasPanel::StaticClass(), layer->WidgetTree))
                           : nullptr;
        if (canvas) {
            layer->WidgetTree->RootWidget = canvas;
            SDK::Params::CanvasPanel_AddChildToCanvas ac{};
            ac.Content = frame;
            if (Call(canvas, "AddChildToCanvas", ac) && ac.ReturnValue) {
                SDK::Params::CanvasPanelSlot_SetAnchors an{};
                an.InAnchors.Minimum = SDK::FVector2D{0.5f, 0.5f};
                an.InAnchors.Maximum = SDK::FVector2D{0.5f, 0.5f};
                Call(ac.ReturnValue, "SetAnchors", an);
                SDK::Params::CanvasPanelSlot_SetAlignment al{};
                al.InAlignment = SDK::FVector2D{0.5f, 0.5f};
                Call(ac.ReturnValue, "SetAlignment", al);
                SDK::Params::CanvasPanelSlot_SetPosition po{};
                po.InPosition = SDK::FVector2D{0.0f, 0.0f};
                Call(ac.ReturnValue, "SetPosition", po);
                SDK::Params::CanvasPanelSlot_SetAutoSize as{};
                as.InbAutoSize = true;
                Call(ac.ReturnValue, "SetAutoSize", as);
                SDK::Params::UserWidget_AddToViewport av{};
                av.ZOrder = 50;
                Call(layer, "AddToViewport", av);
                p.layer = layer;
                placed = true;
            }
        }
    }
    if (!placed) {
        SDK::Params::VerticalBox_AddChildToVerticalBox ad{};
        ad.Content = frame;
        if (!Call(p.box, "AddChildToVerticalBox", ad) || !ad.ReturnValue) return;
        SDK::Params::VerticalBoxSlot_SetHorizontalAlignment sh{};
        sh.InHorizontalAlignment = SDK::EHorizontalAlignment::HAlign_Center;
        Call(ad.ReturnValue, "SetHorizontalAlignment", sh);
    }
    p.frame = frame;
    p.inner = inner;
}

void AddText(const std::string& text, float scale = 1.0f, bool dim = false) {
    auto& p = P();
    SDK::UObject* outer = p.title->WidgetTree ? static_cast<SDK::UObject*>(p.title->WidgetTree) : static_cast<SDK::UObject*>(p.title);
    Add(NewText(outer, text, scale, dim));
}

void SetVisible(SDK::UWidget* w, bool visible) {
    if (!w || !ue::IsValid(w)) return;
    SDK::Params::Widget_SetVisibility p{};
    p.InVisibility = visible ? SDK::ESlateVisibility::Visible : SDK::ESlateVisibility::Collapsed;
    Call(w, "SetVisibility", p);
}

SDK::UObject* TreeOf(SDK::UObject* w) {
    auto* uw = static_cast<SDK::UUserWidget*>(w);
    return uw && uw->WidgetTree ? static_cast<SDK::UObject*>(uw->WidgetTree) : w;
}

// The knight's face `head` (0..6) as a picture of size x size.
SDK::UImage* NewFace(int head, float size) {
    auto& p = P();
    SDK::UObject* io = SDK::UGameplayStatics::SpawnObject(SDK::UImage::StaticClass(), TreeOf(p.title));
    if (!io) return nullptr;
    auto* img = static_cast<SDK::UImage*>(io);
    char path[96];
    std::snprintf(path, sizeof(path), "/Game/NPCs/Knight/Knight_Face_%02d_TX.Knight_Face_%02d_TX", head, head);
    if (SDK::UObject* tex = ue::LoadObject(path); tex && tex->IsA(SDK::UTexture2D::StaticClass())) {
        SDK::Params::Image_SetBrushFromTexture bt{};
        bt.Texture = static_cast<SDK::UTexture2D*>(tex);
        bt.bMatchSize = false;
        Call(img, "SetBrushFromTexture", bt);
    }
    SDK::Params::Image_SetBrushSize bs{};
    bs.DesiredSize = SDK::FVector2D{size, size};
    Call(img, "SetBrushSize", bs);
    return img;
}

SDK::UHorizontalBox* NewRow() {
    SDK::UObject* o = SDK::UGameplayStatics::SpawnObject(SDK::UHorizontalBox::StaticClass(), TreeOf(P().title));
    return o ? static_cast<SDK::UHorizontalBox*>(o) : nullptr;
}

// Adds `w` to a row; fill = takes the row's spare width.
void RowAdd(SDK::UHorizontalBox* row, SDK::UWidget* w, float padRight, bool fill = false) {
    if (!row || !w) return;
    SDK::Params::HorizontalBox_AddChildToHorizontalBox hb{};
    hb.Content = w;
    if (!Call(row, "AddChildToHorizontalBox", hb) || !hb.ReturnValue) return;
    SDK::Params::HorizontalBoxSlot_SetPadding hp{};
    hp.InPadding = SDK::FMargin{0.0f, 0.0f, padRight, 0.0f};
    Call(hb.ReturnValue, "SetPadding", hp);
    struct { SDK::EVerticalAlignment a; } mid{SDK::EVerticalAlignment::VAlign_Center};
    if (SDK::UFunction* f = ue::FindFunction(hb.ReturnValue, "SetVerticalAlignment")) ue::CallFunction(hb.ReturnValue, f, &mid);
    if (fill) {
        struct { SDK::FSlateChildSize s; } sz{};
        sz.s.Value = 1.0f;
        sz.s.SizeRule = SDK::ESlateSizeRule::Fill;
        if (SDK::UFunction* f = ue::FindFunction(hb.ReturnValue, "SetSize")) ue::CallFunction(hb.ReturnValue, f, &sz);
    }
}

const SDK::FLinearColor kReady{0.45f, 0.9f, 0.5f, 1.0f};
const SDK::FLinearColor kWaiting{0.95f, 0.32f, 0.18f, 1.0f};
const SDK::FLinearColor kHostCol{1.0f, 0.84f, 0.42f, 1.0f};
const SDK::FLinearColor kPlaying{0.7f, 0.68f, 0.72f, 1.0f};

// One player: face, name, a coloured badge.
void AddPlayerRow(const sync::LobbyRow& r, const sync::LobbyView& v) {
    SDK::UHorizontalBox* row = NewRow();
    if (!row) return;
    const int head = r.isLocal ? sync::LookLocalHead() : sync::LookHeadOf(r.slot);
    RowAdd(row, NewFace(head < 0 ? 0 : head, 44.0f), 12.0f);
    const std::string name = r.isLocal ? TrF("coop.you", r.name) : r.name;
    RowAdd(row, NewText(TreeOf(P().title), name, 0.85f), 18.0f);
    const char* key;
    const SDK::FLinearColor* col;
    if (r.isHost) { key = "page.badge_host"; col = &kHostCol; }
    else if (!v.onTitle || v.started) { key = "page.badge_playing"; col = &kPlaying; }
    else if (r.ready) { key = "page.badge_ready"; col = &kReady; }
    else { key = "page.badge_waiting"; col = &kWaiting; }
    // the badge: a coloured pill (the game's text style keeps its own colour, so the colour is the pill's)
    SDK::UCommonTextBlock* badge = NewText(TreeOf(P().title), Tr(key), 0.7f);
    auto* pill = static_cast<SDK::UBorder*>(SDK::UGameplayStatics::SpawnObject(SDK::UBorder::StaticClass(), TreeOf(P().title)));
    if (pill && badge) {
        SDK::Params::Border_SetBrushColor bc{};
        bc.InBrushColor = SDK::FLinearColor{col->R * 0.45f, col->G * 0.45f, col->B * 0.45f, 1.0f};
        Call(pill, "SetBrushColor", bc);
        SDK::Params::Border_SetPadding bp{};
        bp.InPadding = SDK::FMargin{10.0f, 2.0f, 10.0f, 2.0f};
        Call(pill, "SetPadding", bp);
        SDK::Params::ContentWidget_SetContent sc{};
        sc.Content = badge;
        Call(pill, "SetContent", sc);
        RowAdd(row, pill, 0.0f);
    } else {
        RowAdd(row, badge, 0.0f);
    }
    Add(row);
}

// The page is taller than the menu: the logo makes room while it is open, and the column moves up into that space.
void MakeRoom(bool on) {
    auto& p = P();
    if (on) {
        if (SDK::UWidget* logo = p.title->LogoScaleBox; logo && ue::IsValid(logo)) {
            p.logo = logo;
            p.logoWas = logo->Visibility;
            SDK::Params::Widget_SetVisibility hv{};
            hv.InVisibility = SDK::ESlateVisibility::Hidden;
            Call(logo, "SetVisibility", hv);
        }
        float shift = 380.0f;   // slate units; worked out from the viewport below when it can be
        SDK::UObject* lib = SDK::UWidgetLayoutLibrary::GetDefaultObj();
        SDK::Params::WidgetLayoutLibrary_GetViewportSize vs{};
        vs.WorldContextObject = p.title;
        SDK::Params::WidgetLayoutLibrary_GetViewportScale sc{};
        sc.WorldContextObject = p.title;
        if (lib && Call(lib, "GetViewportSize", vs) && Call(lib, "GetViewportScale", sc) && sc.ReturnValue > 0.01f && vs.ReturnValue.Y > 0)
            shift = vs.ReturnValue.Y / sc.ReturnValue * 0.45f;
        SDK::Params::Widget_SetRenderTranslation tr{};
        tr.Translation = SDK::FVector2D{0.0f, -shift};
        Call(p.box, "SetRenderTranslation", tr);
    } else {
        if (p.logo && ue::IsValid(p.logo)) {
            SDK::Params::Widget_SetVisibility hv{};
            hv.InVisibility = p.logoWas;
            Call(p.logo, "SetVisibility", hv);
        }
        p.logo = nullptr;
        if (p.box && ue::IsValid(p.box)) {
            SDK::Params::Widget_SetRenderTranslation tr{};
            tr.Translation = SDK::FVector2D{0.0f, 0.0f};
            Call(p.box, "SetRenderTranslation", tr);
        }
    }
}

// ---- what the page shows

std::string DifficultyName(const sync::DifficultyInfo& d) {
    std::string cur = config::Get().difficulty;
    if (!d.isHost && d.players > 1) {
        const float extra = static_cast<float>(d.players - 1);
        const float hp = (d.normal - 1.0f) / extra, dmg = (d.damage - 1.0f) / extra;
        cur = (std::fabs(hp - 0.12f) < 0.01f && dmg < 0.01f)                    ? "normal"
              : (std::fabs(hp - 1.0f) < 0.01f && std::fabs(dmg - 0.25f) < 0.01f) ? "hard"
                                                                                : "custom";
    }
    return cur;
}

std::string StateKey() {
    std::string k;
    const bool ok = coop::SessionReady();
    const net::SessionState st = ok ? coop::Session().State() : net::SessionState::Idle;
    k += std::to_string(static_cast<int>(st)) + "|";
    const sync::LobbyView v = sync::LobbyViewNow();
    k += v.isHost ? "h" : "c";
    k += v.allReady ? "a" : "-";
    k += v.localReady ? "r" : "-";
    k += v.started ? "s" : "-";
    for (const auto& r : v.rows) k += "|" + r.name + (r.ready ? "+" : "-") + std::to_string(sync::LookHeadOf(r.slot));
    if (ok && (st == net::SessionState::Idle || st == net::SessionState::Failed))
        for (const auto& l : coop::Session().FriendLobbies()) k += "|L" + std::to_string(l.lobbyId) + ":" + std::to_string(l.members);
    k += "|" + coop::LocalName();
    k += "|h" + std::to_string(sync::LookLocalHead());
    k += "|d" + DifficultyName(sync::GetDifficultyInfo());
    k += "|" + config::Get().language;
    return k;
}

void Clear() {
    auto& p = P();
    if (p.inner && ue::IsValid(p.inner)) {
        char none = 0;
        Call(p.inner, "ClearChildren", none);
    }
    p.added.clear();
    p.actions.clear();
    p.nameBox = nullptr;
}

void Close();

void SaveName() {
    auto& p = P();
    if (!p.nameBox || !ue::IsValid(p.nameBox)) return;
    SDK::Params::EditableTextBox_GetText g{};
    Call(p.nameBox, "GetText", g);
    const std::string name = SDK::UKismetTextLibrary::Conv_TextToString(g.ReturnValue).ToString();
    coop::SetLocalName(name);
    Toast(TrF("page.name_saved", name), ToastKind::Success, 3.0f);
}

void Build() {
    auto& p = P();
    Clear();
    const bool ok = coop::SessionReady();
    const net::SessionState st = ok ? coop::Session().State() : net::SessionState::Idle;
    const bool idle = st == net::SessionState::Idle || st == net::SessionState::Failed;
    const sync::LobbyView v = sync::LobbyViewNow();

    AddText(Tr("page.title"), 1.3f);

    if (!coop::CoopAllowed()) {
        AddText(Tr(coop::CoopDisabled() ? "own.failed" : "coop.verifying"), 0.7f, true);
    } else if (idle) {
        Add(TextButton(Tr("page.host"), [] { coop::Session().Host(); }));
        for (const auto& l : coop::Session().FriendLobbies()) {
            const uint64_t id = l.lobbyId;
            const std::string host = l.hostName.empty() ? std::string(Tr("coop.unknown_host")) : l.hostName;
            Add(TextButton(TrF("page.join", host, l.members, l.maxMembers), [id] { coop::Session().JoinLobby(id); }));
        }
        if (coop::Session().FriendLobbies().empty()) AddText(Tr("page.no_lobbies"), 0.7f, true);
        if (st == net::SessionState::Failed && !coop::Session().LastError().empty())
            AddText(TrF("coop.state.failed", coop::Session().LastError()), 0.65f, true);
    } else if (st == net::SessionState::CreatingLobby || st == net::SessionState::JoiningLobby || st == net::SessionState::Connecting) {
        AddText(Tr("page.connecting"), 0.8f, true);
        Add(TextButton(Tr("page.leave"), [] { coop::Session().Leave(); }));
    } else {
        // the players, each with where they are
        for (const auto& r : v.rows) AddPlayerRow(r, v);
        // Steam's own invite window: pick friends, they get a "join game" invite
        Add(TextButton(Tr("page.invite"), [] {
            if (!coop::Session().OpenInviteOverlay()) Toast(Tr("page.invite_failed"), ToastKind::Error, 4.0f);
        }));
        if (v.onTitle && !v.started) {
            if (v.isHost) {
                if (v.allReady) Add(TextButton(Tr("lobby.play_now"), [] { Close(); sync::LobbyHostPlayNow(); }));
                else AddText(Tr("lobby.host_wait"), 0.7f, true);
            } else {
                const bool ready = v.localReady;
                Add(TextButton(Tr(ready ? "lobby.not_ready" : "lobby.ready"), [ready] { sync::LobbySetReady(!ready); }));
            }
        }
        Add(TextButton(Tr("page.leave"), [] { coop::Session().Leave(); }));
    }

    // difficulty: the host picks (each press: the next one), the others see it
    {
        const sync::DifficultyInfo d = sync::GetDifficultyInfo();
        const std::string cur = DifficultyName(d);
        const std::string label = TrF("page.difficulty", Tr(("diff." + cur).c_str()));
        if (d.isHost) {
            Add(TextButton(label, [cur] {
                const std::string next = cur == "normal" ? "hard" : "normal";
                const auto& c = config::Get();
                config::SetDifficulty(next, c.hpScale, c.damageScale);
                sync::DifficultyChanged();
            }));
        } else {
            AddText(label + "  " + Tr("page.host_picks"), 0.75f, true);
        }
    }

    // your name: a text box in the menu's font, saved with the button below it
    {
        SDK::UObject* outer = p.title->WidgetTree ? static_cast<SDK::UObject*>(p.title->WidgetTree) : static_cast<SDK::UObject*>(p.title);
        if (SDK::UObject* o = SDK::UGameplayStatics::SpawnObject(SDK::UEditableTextBox::StaticClass(), outer)) {
            auto* box = static_cast<SDK::UEditableTextBox*>(o);
            if (p.model && ue::IsValid(p.model)) {
                CopyFont(box->WidgetStyle.Font, p.model->Font, 0.8f);
                CopyFont(box->Font, p.model->Font, 0.8f);
            }
            SDK::Params::EditableTextBox_SetText t{};
            t.InText = ToText(coop::LocalName());
            Call(box, "SetText", t);
            p.nameBox = box;
            if (SDK::UHorizontalBox* row = NewRow()) {
                RowAdd(row, NewText(TreeOf(p.title), Tr("page.your_name"), 0.75f, true), 10.0f);
                RowAdd(row, box, 10.0f, true);
                SDK::UFS_Button_C* save = TextButton(Tr("page.save"), [] { SaveName(); });
                RowAdd(row, save, 0.0f);
                Add(row);
            }
        }
    }

    // your look: the knight's faces in a row, the chosen one bright
    {
        Add(NewText(TreeOf(p.title), Tr("page.your_look"), 0.7f, true), 18.0f);
        SDK::UObject* outer = p.title->WidgetTree ? static_cast<SDK::UObject*>(p.title->WidgetTree) : static_cast<SDK::UObject*>(p.title);
        if (SDK::UObject* o = SDK::UGameplayStatics::SpawnObject(SDK::UHorizontalBox::StaticClass(), outer)) {
            auto* row = static_cast<SDK::UHorizontalBox*>(o);
            const int chosen = sync::LookLocalHead();   // smaller faces: the row must fit the column
            for (int i = 0; i < net::kLookHeads; ++i) {
                char path[96];
                std::snprintf(path, sizeof(path), "/Game/NPCs/Knight/Knight_Face_%02d_TX.Knight_Face_%02d_TX", i, i);
                SDK::UObject* tex = ue::LoadObject(path);
                SDK::UObject* io = SDK::UGameplayStatics::SpawnObject(SDK::UImage::StaticClass(), outer);
                if (!io) continue;
                auto* img = static_cast<SDK::UImage*>(io);
                if (tex && tex->IsA(SDK::UTexture2D::StaticClass())) {
                    SDK::Params::Image_SetBrushFromTexture bt{};
                    bt.Texture = static_cast<SDK::UTexture2D*>(tex);
                    bt.bMatchSize = false;
                    Call(img, "SetBrushFromTexture", bt);
                }
                SDK::Params::Image_SetBrushSize bs{};
                bs.DesiredSize = SDK::FVector2D{kFaceSize * 0.85f, kFaceSize * 0.85f};
                Call(img, "SetBrushSize", bs);
                SDK::UFS_Button_C* b = NewButton(img);
                if (!b) continue;
                SDK::Params::Widget_SetRenderOpacity op{};
                op.InOpacity = i == chosen ? 1.0f : 0.4f;
                Call(b, "SetRenderOpacity", op);
                p.actions[b] = [i] { sync::LookSetLocalHead(i); };
                SDK::Params::HorizontalBox_AddChildToHorizontalBox hb{};
                hb.Content = b;
                if (Call(row, "AddChildToHorizontalBox", hb) && hb.ReturnValue) {
                    SDK::Params::HorizontalBoxSlot_SetPadding hp{};
                    hp.InPadding = SDK::FMargin{3.0f, 0.0f, 3.0f, 0.0f};
                    Call(hb.ReturnValue, "SetPadding", hp);
                }
            }
            Add(row);
        }
    }

    Add(NewText(TreeOf(p.title), Tr("page.f5_hint"), 0.65f, true), 16.0f);
    Add(TextButton(Tr("page.back"), [] { Close(); }), 10.0f);
    p.shown = StateKey();
}

void Close() {
    auto& p = P();
    if (!p.open) return;
    Clear();
    if (p.frame && ue::IsValid(p.frame)) {
        char none = 0;
        Call(p.frame, "RemoveFromParent", none);
    }
    if (p.layer && ue::IsValid(p.layer)) {
        char none = 0;
        Call(p.layer, "RemoveFromParent", none);
    }
    p.layer = nullptr;
    p.frame = nullptr;
    p.inner = nullptr;
    for (SDK::UWidget* w : p.hidden) SetVisible(w, true);
    p.hidden.clear();
    MakeRoom(false);
    p.open = false;
    QD_INFO(kTag, "closed");
}

void Tick(float dt) {
    auto& p = P();
    if (!p.open) return;
    if (!p.title || !ue::IsValid(p.title) || !p.box || !ue::IsValid(p.box) || !p.inner || !ue::IsValid(p.inner)) {   // the menu went away (a game started)
        p.open = false;
        p.added.clear();
        p.hidden.clear();
        p.actions.clear();
        p.nameBox = nullptr;
        p.frame = nullptr;
        p.inner = nullptr;
        if (p.layer && ue::IsValid(p.layer)) {   // the page's layer must not stay over the game
            char none = 0;
            Call(p.layer, "RemoveFromParent", none);
        }
        p.layer = nullptr;
        return;
    }
    p.lobbiesIn -= dt;
    if (p.lobbiesIn <= 0) {
        p.lobbiesIn = kLobbyListSec;
        if (coop::SessionReady() && coop::CoopAllowed()) coop::Session().RefreshFriendLobbies();
    }
    p.refreshIn -= dt;
    if (p.refreshIn > 0) return;
    p.refreshIn = kRefreshSec;
    if (StateKey() != p.shown) Build();
}

}  // namespace

void CoopPageOpen(SDK::UObject* titleObj) {
    auto& p = P();
    if (p.open || !titleObj || !ue::IsValid(titleObj)) return;
    auto* title = static_cast<SDK::UTitleScreenUI_C*>(titleObj);
    SDK::UFS_Button_C* play = title->LoadGameButton;
    if (!play || !play->Slot || !play->Slot->Parent) return;
    auto* playSlot = static_cast<SDK::UVerticalBoxSlot*>(play->Slot);
    p.title = title;
    p.box = play->Slot->Parent;
    p.size = playSlot->Size;
    p.pad = playSlot->Padding;
    p.pad.Top *= 0.5f;   // the page has more lines than the menu: a tighter column
    p.pad.Bottom *= 0.5f;
    p.h = playSlot->HorizontalAlignment;
    p.v = playSlot->VerticalAlignment;
    p.model = PlayLabel(title);
    p.hidden.clear();
    for (int i = 0; i < p.box->Slots.Num(); ++i) {
        SDK::UPanelSlot* sl = p.box->Slots[i];
        if (!sl || !sl->Content) continue;
        const auto vis = sl->Content->Visibility;   // only what shows now comes back on Back
        if (vis == SDK::ESlateVisibility::Collapsed || vis == SDK::ESlateVisibility::Hidden) continue;
        p.hidden.push_back(sl->Content);
        SetVisible(sl->Content, false);
    }
    p.open = true;
    MakeFrame();
    MakeRoom(true);
    {   // the column itself stays where it is (it is empty while the page is open)
        SDK::Params::Widget_SetRenderTranslation tr{};
        tr.Translation = SDK::FVector2D{0.0f, 0.0f};
        Call(p.box, "SetRenderTranslation", tr);
    }
    p.lobbiesIn = 0;
    if (coop::SessionReady() && coop::CoopAllowed()) coop::Session().RefreshFriendLobbies();
    Build();
    QD_INFO(kTag, "opened");
}

bool CoopPageIsOpen() { return P().open; }

#if QD_DEV
void CoopPageDebugOpen() {
    SDK::UClass* cls = ue::FindClass("/Game/UI/Title/TitleScreenUI.TitleScreenUI_C");
    auto& objs = SDK::UObject::GObjects;
    for (int i = 0; cls && i < objs->Num(); ++i) {
        SDK::UObject* o = objs->GetByIndex(i);
        if (o && !o->IsDefaultObject() && o->IsA(cls) && ue::IsValid(o)) {
            char none = 0;   // past a first run's intro and language screen, as the lab's newgame does
            Call(o, "SkipIntroSplashes", none);
            Call(o, "ConfirmLanguage", none);
            if (P().open) Close();
            gamethread::Post([o] { if (ue::IsValid(o)) CoopPageOpen(o); });
            return;
        }
    }
    QD_WARN(kTag, "page: no main menu");
}
#endif

void CoopPageInit() {
    auto& p = P();
    if (p.inited) return;
    p.inited = true;
    hooks::AddPost(kButtonClicked, [](hooks::CallInfo& ci) {
        auto& pg = P();
        if (!pg.open) return;
        auto it = pg.actions.find(ci.object);
        if (it == pg.actions.end()) return;
        auto action = it->second;   // the action may rebuild the page (and the map)
        gamethread::Post([action] { if (action) action(); });
    });
    gamethread::AddTicker("coop-page", [](float dt) { Tick(dt); });
    QD_INFO(kTag, "initialized");
}

}  // namespace qd::ui
