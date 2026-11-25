// Knights' Domain — string tables. English is the reference text; Chinese is the translation.
#include "qdcoop/ui/I18n.h"

#include "qdcoop/core/Config.h"
#include "qdcoop/core/Core.h"
#include "qdcoop/core/Log.h"
#include "qdcoop/core/Ownership.h"

#include <atomic>
#include <string_view>
#include <unordered_map>

namespace qd::ui {
namespace {

constexpr char kTag[] = "i18n";

struct Entry {
    const char* key;
    const char* en;
    const char* zh;   // nullptr = fall back to English
    const char* ja = nullptr;   // Japanese; nullptr = fall back to English
};

// clang-format off
constexpr Entry kTable[] = {
    // ---- F5 window
    {"menu.footer",            "Press {} to close this menu",                       "按 {} 关闭菜单",
                               "{} でメニューを閉じる"},
    {"menu.empty",             "(No pages available yet)",                          "（还没有可用的页面）",
                               "（表示できるページがまだありません）"},
    {"tab.coop",               "Co-op",                                             "联机",
                               "協力プレイ"},
    {"tab.name",               "My Name",                                           "我的名字",
                               "名前"},
    {"tab.summon",             "Teleport",                                          "传送",
                               "テレポート"},
    {"tab.about",              "About",                                             "关于",
                               "このMODについて"},

    // ---- update banner + what's-new notice
    {"update.available",       "Version {} is available (installed: {}).",        "有新版本 {}（当前 {}）",
                               "バージョン {} が利用できます（現在 {}）"},
    {"update.button",          "Update now",                                        "立即更新",
                               "今すぐ更新"},
    {"update.downloading",     "Downloading the update… {}%",                       "正在下载更新… {}%",
                               "更新をダウンロード中… {}%"},
    {"update.installed",       "Updated to {}. Restart the game to finish.",       "已更新到 {}，重启游戏后生效",
                               "{} に更新しました。ゲームを再起動すると有効になります"},
    {"update.failed",          "The update could not be installed ({}). You can try again, or download it from GitHub or the website.",
                               "更新安装失败（{}）。可以再试一次，或到网站 / GitHub 手动下载",
                               "更新をインストールできませんでした（{}）。もう一度試すか、GitHub / サイトからダウンロードしてください"},
    {"update.retry",           "Try again",                                         "再试一次",
                               "もう一度試す"},
    {"notice.title",           "What's new in {}",                                  "{} 更新公告",
                               "{} の更新内容"},
    {"notice.thanks",          "This is the Co-op Anything build of Knights' Domain by Mangolotis, published with their permission. The whole game is now shared by the party:",
                               "这是 Co-op Anything 制作的 Knights' Domain（作者 Mangolotis）联机版本，经作者许可发布。现在整个游戏由队伍共享：",
                               "これは Mangolotis 作 Knights' Domain の Co-op Anything 版で、作者の許可を得て公開しています。ゲーム全体がパーティで共有されます："},
    {"notice.1",               "1. The world: chests, doors, levers, gates, pickups, checkpoints and story progress change for everyone",
                               "1. 世界：宝箱、门、拉杆、闸门、拾取物、存档点和剧情进度对所有人同步",
                               "1. ワールド：宝箱、扉、レバー、門、アイテム、チェックポイント、ストーリー進行が全員に反映されます"},
    {"notice.2",               "2. Every boss fight, with arena locks, adds, rewards and defeat shared, and no replayed entrance",
                               "2. 所有 BOSS 战：场地封锁、小怪、奖励和击败状态全员共享，不再重复登场",
                               "2. すべてのボス戦：アリーナの封鎖、取り巻き、報酬、撃破が共有され、登場演出も繰り返されません"},
    {"notice.3",               "3. Enemy projectiles, area attacks and loot exist for every player",
                               "3. 怪物的飞行道具、范围攻击和掉落物对每位玩家都存在",
                               "3. 敵の飛び道具、範囲攻撃、ドロップが全プレイヤーに存在します"},
    {"notice.4",               "4. Party tab: revive downed teammates, rest together, trade items; host difficulty setting in the Co-op tab",
                               "4. 队伍页：救起倒地队友、一起休息、交易物品；房主可在联机页设置难度",
                               "4. パーティタブ：倒れた仲間の蘇生、一緒に休憩、アイテム交換。ホストは協力タブで難易度を設定できます"},
    {"notice.5",               "5. Waiting room: everyone stays on the main menu and readies up; the host picks the save and everybody starts together",
                               "5. 等候室：所有人留在主菜单并准备；房主选择存档后所有人同时进入游戏",
                               "5. 待合室：全員がメインメニューで準備し、ホストがセーブを選ぶと全員が同時に始まります"},
    {"notice.7",               "7. The host keeps every friend's character: join and you play your co-op character in the host's world, next to the host; your own saves are not touched",
                               "7. 房主保存每位好友的角色：加入后你会在房主的世界里、房主身边使用你的联机角色；不会改动你自己的存档",
                               "7. ホストが仲間のキャラクターを保管：参加するとホストの世界で、ホストのそばに協力プレイ用キャラクターで登場します。自分のセーブは変更されません"},
    {"notice.6",               "6. Items are shared: anything that is not a consumable (weapons, gear, necklaces, keys...) that one player picks up or is given goes to everyone; shop purchases, consumables and coins are not",
                               "6. 物品共享：任何玩家拾取或获得的非消耗品（武器、装备、项链、钥匙等）全员都有；商店购买的物品、消耗品和金币不共享",
                               "6. アイテム共有：誰かが拾ったりもらったりした消耗品以外の物（武器、装備、ネックレス、鍵など）は全員に渡ります。購入品、消耗品、コインは共有されません"},
    {"notice.disclaimer",      "This mod has no connection with the developer of Queen's Domain. If you run into a bug while using it, please do not report it to the official developer. Thank you!",
                               "本 MOD 与 Queen's Domain 开发商没有任何关系。如果使用本 MOD 时发生任何 bug，请不要提交给官方，谢谢配合！",
                               "このMODは Queen's Domain の開発元とは一切関係ありません。使用中に不具合が起きても、公式には報告しないでください。ご協力ありがとうございます！"},
    {"notice.ok",              "Got it",                                            "知道了",
                               "了解"},
    {"toast.game_updated",     "Queen's Domain was updated and this version of Knights' Domain does not know it yet. Co-op may misbehave; please update the mod.",
                               "Queen's Domain 已更新，当前版本的 Knights' Domain 还不认识它，联机可能出现异常，请更新 MOD",
                               "Queen's Domain が更新されました。この版の Knights' Domain は未対応のため、協力プレイに不具合が出る場合があります。MOD を更新してください"},
    {"toast.boss_talk",        "{} is speaking with a boss. The fight starts when they finish.",
                               "{} 正在和 Boss 对话，对话结束后开战",
                               "{} がボスと会話中。終わると戦闘が始まります"},
    // ---- Co-op page
    {"coop.loading",           "Loading…",                                          "加载中…",
                               "読み込み中…"},
    {"coop.language",          "Language",                                          "语言",
                               "言語"},
    {"coop.init_failed",       "The co-op module failed to start. See KnightsDomain.log.",
                               "联机模块没有初始化成功，详见 KnightsDomain.log。",
                               "協力プレイ機能を起動できませんでした。KnightsDomain.log を確認してください。"},
    {"coop.verifying",         "Checking your Steam copy of Queen's Domain…",       "正在验证 Steam 正版 Queen's Domain…",
                               "Steam 版 Queen's Domain を確認しています…"},
    {"coop.starting",          "Waiting for Steam…",                                "正在等待 Steam…",
                               "Steam を待っています…"},
    {"coop.status",            "Status: ",                                          "状态：",
                               "状態："},
    {"coop.state.idle",        "Not in a session",                                  "未联机",
                               "未接続"},
    {"coop.state.creating",    "Creating room…",                                    "正在创建房间…",
                               "ルームを作成中…"},
    {"coop.state.hosting_n",   "Host ({} players)",                                 "房主（{} 人）",
                               "ホスト（{} 人）"},
    {"coop.state.hosting_wait","Host (waiting for friends to join)",                "房主（等待好友加入）",
                               "ホスト（フレンドの参加を待っています）"},
    {"coop.state.joining",     "Joining…",                                          "正在加入…",
                               "参加中…"},
    {"coop.state.connected",   "Connected",                                         "已连接",
                               "接続済み"},
    {"coop.state.handshake",   "Connected (handshaking…)",                          "已连接（握手中…）",
                               "接続済み（認証中…）"},
    {"coop.state.failed",      "Failed: {}",                                        "失败：{}",
                               "失敗：{}"},
    {"common.unknown_error",   "unknown error",                                     "未知错误",
                               "不明なエラー"},
    {"coop.host",              "Host a co-op room",                                 "创建联机房间",
                               "協力プレイのルームを作成"},
    {"coop.leave",             "Leave",                                             "离开",
                               "退出"},
    {"diff.title",             "Difficulty (set by the host)",                      "难度（由房主设置）",
                               "難易度（ホストが設定）"},
    {"diff.normal",            "Normal",                                            "普通",
                               "ノーマル"},
    {"diff.hard",              "Hard",                                              "困难",
                               "ハード"},
    {"diff.custom",            "Custom",                                            "自定义",
                               "カスタム"},
    {"diff.hp_slider",         "Enemy health per extra player",                     "每多一名玩家敌人生命",
                               "プレイヤー1人ごとの敵の体力"},
    {"diff.dmg_slider",        "Enemy damage per extra player",                     "每多一名玩家敌人伤害",
                               "プレイヤー1人ごとの敵の攻撃力"},
    {"diff.host_only",         "Only the host can change the difficulty.",          "只有房主可以更改难度。",
                               "難易度を変更できるのはホストだけです。"},
    {"diff.effect",            "With {} players: enemy health +{}% (elites +{}%, bosses +{}%), enemy damage +{}%",
                               "{} 名玩家时：敌人生命 +{}%（精英 +{}%，首领 +{}%），敌人伤害 +{}%",
                               "{} 人の場合：敵の体力 +{}%（エリート +{}%、ボス +{}%）、敵の攻撃力 +{}%"},
    {"coop.members",           "Members",                                           "成员",
                               "メンバー"},
    {"coop.col.slot",          "Slot",                                              "槽位",
                               "スロット"},
    {"coop.col.name",          "Name",                                              "名字",
                               "名前"},
    {"coop.col.ping",          "Ping",                                              "延迟",
                               "Ping"},
    {"coop.host_tag",          "Host",                                              "房主",
                               "ホスト"},
    {"coop.you",               "{} (you)",                                          "{}（你）",
                               "{}（あなた）"},
    {"coop.friends",           "Friends' rooms",                                    "好友的房间",
                               "フレンドのルーム"},
    {"coop.refresh",           "Refresh",                                           "刷新",
                               "更新"},
    {"coop.invite",            "Friend invite received",                            "收到好友邀请",
                               "フレンドから招待が届きました"},
    {"coop.accept",            "Accept invite",                                     "接受邀请",
                               "招待を受ける"},
    {"coop.no_lobbies",        "(No friends are hosting right now. Click Refresh to check again.)",
                               "（没有好友正在开房，点「刷新」再看看）",
                               "（ルームを開いているフレンドはいません。「更新」で再確認できます）"},
    {"coop.col.host",          "Host",                                              "房主",
                               "ホスト"},
    {"coop.col.players",       "Players",                                           "人数",
                               "人数"},
    {"coop.unknown_host",      "(unknown)",                                         "（未知）",
                               "（不明）"},
    {"coop.version_diff",      "Different version",                                 "版本不同",
                               "バージョン違い"},
    {"coop.join",              "Join",                                              "加入",
                               "参加"},

    // ---- ownership
    {"own.failed",             "Knights' Domain needs Queen's Domain launched from Steam with an owned copy. Co-op is disabled.",
                               "Knights' Domain 需要从 Steam 启动并拥有正版的 Queen's Domain。联机功能已停用。",
                               "Knights' Domain は、所有している Queen's Domain を Steam から起動したときに使えます。協力プレイは無効になっています。"},

    // ---- My Name page
    {"name.current",           "Current name: ",                                    "当前名字：",
                               "現在の名前："},
    {"name.slot",              "Current save slot: slot{}",                         "当前存档槽：slot{}",
                               "現在のセーブスロット：slot{}"},
    {"name.save",              "Save",                                              "保存",
                               "保存"},
    {"name.reset",             "Reset to default",                                  "恢复默认",
                               "初期設定に戻す"},
    {"name.help1",             "Your name belongs to the current save slot: other save slots keep their own name. If none is set, your Steam name is used.",
                               "名字跟当前存档槽绑定：换到别的存档槽会用那个槽自己的名字。没有设置时用 Steam 昵称。",
                               "名前は現在のセーブスロットごとに保存されます。別のスロットではそのスロットの名前を使います。未設定のときは Steam の名前を使います。"},
    {"name.help2",             "Up to 24 characters. Renaming during co-op updates everyone right away.",
                               "最多 24 个字。联机中改名会立刻同步给其他人。",
                               "最大 24 文字。協力プレイ中に変更すると、すぐに全員に反映されます。"},
    {"name.saved",             "Name saved: {}",                                    "名字已保存：{}",
                               "名前を保存しました：{}"},
    {"name.save_failed",       "Name applied, but profiles.json could not be written",
                               "名字已生效，但写入 profiles.json 失败",
                               "名前は反映されましたが、profiles.json に保存できませんでした"},
    {"name.default",           "Player",                                            "玩家",
                               "プレイヤー"},

    // ---- handshake / roster toasts
    {"reject.full",            "The room is full",                                  "房间已满",
                               "ルームが満員です"},
    {"reject.mod",             "Different mod version. Both players need the same version of Knights' Domain.",
                               "MOD 版本不一致，请双方更新到同一版本的 Knights' Domain",
                               "MOD のバージョンが違います。全員が同じバージョンの Knights' Domain を使ってください。"},
    {"reject.game",            "Different game version. Both players need the same version of Queen's Domain.",
                               "游戏版本不一致，请双方把游戏更新到同一版本",
                               "ゲームのバージョンが違います。全員が同じバージョンの Queen's Domain を使ってください。"},
    {"reject.protocol",        "Different co-op protocol. Both players need to update Knights' Domain.",
                               "联机协议版本不一致，请双方更新 MOD",
                               "通信プロトコルが違います。全員が Knights' Domain を更新してください。"},
    {"reject.kicked",          "The host removed you from the room",                "你被房主移出了房间",
                               "ホストによってルームから外されました"},
    {"reject.unknown",         "unknown reason",                                    "未知原因",
                               "不明な理由"},
    {"toast.rejected_join",    "Declined a join request: {}",                       "拒绝了一个加入请求：{}",
                               "参加リクエストを断りました：{}"},
    {"toast.joined",           "{} joined",                                         "{} 加入了",
                               "{} が参加しました"},
    {"toast.left",             "{} left",                                           "{} 离开了",
                               "{} が退出しました"},
    {"toast.renamed",          "{} is now called {}",                               "{} 改名为 {}",
                               "{} が {} に名前を変更しました"},
    {"toast.host_left",        "The host left",                                     "房主已离开",
                               "ホストが退出しました"},
    {"toast.joined_room",      "Joined {}'s room",                                  "已加入 {} 的房间",
                               "{} のルームに参加しました"},
    {"toast.host_default",     "the host",                                          "房主",
                               "ホスト"},
    {"toast.cannot_join",      "Cannot join: {}",                                   "无法加入：{}",
                               "参加できません：{}"},
    {"toast.room_created",     "Co-op room created. Waiting for friends to join.",  "联机房间已创建，等待好友加入",
                               "協力プレイのルームを作成しました。フレンドの参加を待っています。"},
    {"toast.session_failed",   "Co-op failed: {}",                                  "联机失败：{}",
                               "協力プレイに失敗しました：{}"},
    {"toast.host_timeout",     "Could not reach the host (no response)",            "连接房主超时（没有收到回应）",
                               "ホストに接続できませんでした（応答なし）"},
    {"toast.init_failed",      "The co-op module failed to start ({}). See KnightsDomain.log.",
                               "联机模块初始化失败（{}），详见 KnightsDomain.log",
                               "協力プレイ機能を起動できませんでした（{}）。KnightsDomain.log を確認してください。"},
    {"toast.left_session",     "You left the co-op session",                        "已离开联机",
                               "協力プレイから退出しました"},

    // ---- Teleport page / toasts
    {"summon.done",            "Teleported", "已传送",
                               "ホストのところへテレポートしました"},
    {"summon.only_client",     "Teleport works once you are in a co-op room with someone",
                               "只有加入别人的房间时才能传送到房主身边",
                               "ホストへのテレポートは、ほかの人のルームに参加しているときだけ使えます"},
    {"summon.no_world",        "You are not in the game world yet",                 "还没有进入游戏世界",
                               "まだゲームの世界に入っていません"},
    {"summon.dead",            "You can't teleport while dead",                     "死亡状态下不能传送",
                               "死亡中はテレポートできません"},
    {"summon.no_pos",          "The host's position hasn't arrived yet. Please try again in a moment.",
                               "还没收到房主的位置，请稍后再试",
                               "ホストの位置をまだ受信していません。少し待ってからもう一度試してください。"},
    {"summon.other_map",       "The host is on another map, so you can't teleport there directly yet",
                               "房主在另一张地图，暂时不能直接传送",
                               "ホストは別のマップにいるため、今は直接テレポートできません"},
    {"summon.failed",          "Teleport failed (see KnightsDomain.log)",           "传送失败（详见 KnightsDomain.log）",
                               "テレポートに失敗しました（KnightsDomain.log を確認してください）"},
    {"summon.in_progress",     "Teleporting to the host…",                          "正在传送到房主身边…",
                               "ホストのところへテレポート中…"},
    {"summon.back_at_save",    "You're back at your save point. F5 > Teleport takes you back to the host.",
                               "已回到存档点，F5 → 传送 可以回到房主身边",
                               "セーブ地点に戻りました。F5 → テレポートでホストのところへ戻れます。"},
    // ---- waiting room
    {"page.title", "Co-op", "联机", "協力プレイ"},
    {"page.host", "Host a lobby", "创建房间", "ロビーを作る"},
    {"page.join", "Join {} ({}/{})", "加入 {}（{}/{}）", "{} に参加（{}/{}）"},
    {"page.no_lobbies", "No friends in a lobby right now", "现在没有好友在房间里", "ロビーにいるフレンドはいません"},
    {"page.connecting", "Connecting...", "连接中...", "接続中..."},
    {"page.leave", "Leave lobby", "离开房间", "ロビーを出る"},
    {"page.invite", "Invite friends", "邀请好友", "フレンドを招待"},
    {"page.invite_failed", "The Steam overlay is not available (is it turned on in Steam?)", "Steam 界面不可用（是否已在 Steam 中开启？）", "Steam オーバーレイが使えません（Steam で有効になっていますか？）"},
    {"page.invite_hint", "Invite friends opens Steam: they get a join invite", "邀请好友会打开 Steam：好友会收到加入邀请", "「フレンドを招待」で Steam が開き、参加の招待が届きます"},
    {"page.difficulty", "Difficulty: {}", "难度：{}", "難易度：{}"},
    {"page.host_picks", "(the host picks)", "（由房主选择）", "（ホストが選びます）"},
    {"page.your_name", "Your name", "你的名字", "あなたの名前"},
    {"page.save_name", "Save name", "保存名字", "名前を保存"},
    {"page.name_saved", "Name saved: {}", "名字已保存：{}", "名前を保存しました：{}"},
    {"page.your_look", "Your look (how the others see you)", "你的外观（其他人看到的你）", "見た目（ほかの人から見たあなた）"},
    {"page.badge_host", "HOST", "房主", "ホスト"},
    {"page.badge_ready", "READY", "已准备", "準備OK"},
    {"page.badge_waiting", "NOT READY", "未准备", "未準備"},
    {"page.badge_playing", "PLAYING", "游戏中", "プレイ中"},
    {"page.save", "Save", "保存", "保存"},
    {"page.back", "Back", "返回", "戻る"},
    {"lobby.window_title", "Co-op", "联机", "協力プレイ"},
    {"lobby.close", "Close", "关闭", "閉じる"},
    {"lobby.players", "Players", "玩家", "プレイヤー"},
    {"lobby.play_now", "Play now", "开始游戏", "プレイ開始"},
    {"lobby.you", "You", "你", "あなた"},
    {"lobby.head", "Look", "外观", "見た目"},
    {"lobby.head_help", "how the others see you", "其他人看到的你", "ほかの人から見たあなた"},
    {"lobby.state.playing", "Playing", "游戏中", "プレイ中"},
    {"about.check", "Check for updates", "检查更新", "アップデートを確認"},
    {"about.checking", "Checking for updates...", "正在检查更新...", "アップデートを確認中..."},
    {"about.up_to_date", "You have the newest version.", "已是最新版本。", "最新版です。"},
    {"tab.voice", "Voice", "语音", "ボイス"},
    {"voice.help", "Talk to the others in the game: they hear you from where you stand, louder up close, from the left or right. Push to talk by default.", "在游戏中与其他人语音：他们会从你所在的位置听到你，越近越响，并分左右。默认按键说话。", "ゲーム内で話せます。近いほど大きく、左右の位置も聞こえます。初期設定はプッシュトゥトーク。"},
    {"voice.enabled", "Voice chat on", "开启语音", "ボイスチャットを使う"},
    {"voice.mode", "Mode:", "模式：", "モード："},
    {"voice.mic_device", "Microphone:", "麦克风：", "マイク："},
    {"voice.speaker_device", "Speakers:", "扬声器：", "スピーカー："},
    {"voice.default_device", "Windows default", "Windows 默认", "Windows の既定"},
    {"voice.mic_section", "Microphone", "麦克风", "マイク"},
    {"voice.players", "Players", "玩家", "プレイヤー"},
    {"voice.sending", "sending", "正在发送", "送信中"},
    {"voice.quiet", "not sending", "未发送", "送信していません"},
    {"voice.test_mic", "Test microphone", "测试麦克风", "マイクをテスト"},
    {"voice.test_help", "(talk and watch the bar)", "（说话并观察音量条）", "（話してバーを見てください）"},
    {"voice.threshold_help", "Drag the gold mark just above your room noise: talking passes it, quiet does not.", "把金色标记拖到环境噪音之上：说话时超过它，安静时不超过。", "金色の線を周りの雑音のすぐ上に。話すと超え、静かな時は超えないように。"},
    {"voice.key_also", "Talk key (also sends):", "说话键（同样会发送）：", "話すキー（押しても送信）："},
    {"voice.ptt", "Push to talk", "按键说话", "押して話す"},
    {"voice.open", "Voice activation", "声音激活", "音声で自動送信"},
    {"voice.key", "Talk key:", "说话键：", "話すキー："},
    {"voice.volume", "Voices volume", "语音音量", "声の音量"},
    {"voice.mic_volume", "Microphone volume", "麦克风音量", "マイク音量"},
    {"voice.threshold", "Sensitivity", "灵敏度", "感度"},
    {"voice.range", "Hearing range", "听觉范围", "聞こえる距離"},
    {"voice.not_in_session", "Join or host a co-op room to talk.", "加入或创建联机房间后即可语音。", "協力プレイのルームに入ると話せます。"},
    {"voice.mic_level", "Your microphone:", "你的麦克风：", "あなたのマイク："},
    {"voice.on_air", "on air", "说话中", "送信中"},
    {"voice.no_mic", "No microphone found (check Windows sound settings)", "未找到麦克风（请检查 Windows 声音设置）", "マイクが見つかりません（Windows のサウンド設定を確認）"},
    {"voice.no_speakers", "No speakers found (check Windows sound settings)", "未找到扬声器（请检查 Windows 声音设置）", "スピーカーが見つかりません"},
    {"voice.mute", "Mute", "静音", "ミュート"},
    {"voice.far", "too far away", "距离太远", "遠すぎます"},
    {"voice.talking_short", "talking", "说话中", "話しています"},
    {"voice.volume_at", "heard at {}%", "音量 {}%", "{}% で聞こえます"},
    {"voice.you_talk", "You are talking", "你正在说话", "話しています"},
    {"voice.talking", "{} is talking", "{} 正在说话", "{} が話しています"},
    {"tab.fix", "Fix", "修复", "修正"},
    {"fix.help", "Something stuck or out of sync? These put it right.", "卡住了或不同步？这些可以修复。", "詰まったり同期がずれたら、ここで直せます。"},
    {"fix.lobby", "Co-op lobby", "联机大厅", "協力プレイのロビー"},
    {"fix.teleport", "Teleport to the host", "传送到房主身边", "ホストのところへテレポート"},
    {"fix.resync", "Load the host's world again", "重新载入房主的世界", "ホストのワールドを読み直す"},
    {"fix.resync_done", "Asked the host for its world", "已向房主请求世界状态", "ホストにワールドを要求しました"},
    {"fix.stand_up", "Stand up (stuck while down)", "站起来（倒地卡住时）", "起き上がる（ダウンで詰まった時）"},
    {"lobby.title", "Waiting room", "等候室",
                               "待合室"},
    {"lobby.help_host", "Everyone stays on the main menu. When all players are ready, press Play now and pick the save (or a new game): everybody starts together.",
                               "所有人先留在主菜单。全员准备好后，房主点「开始游戏」并选择存档（或新游戏），所有人同时进入游戏。",
                               "全員がメインメニューで待機します。全員の準備ができたらホストが「Play Game」を押してセーブ（または新規ゲーム）を選ぶと、全員が同時に始まります。"},
    {"lobby.help_client", "Press Ready when you are set. The host picks the save and everybody starts together; your co-op character is kept by the host.",
                               "准备好后点「准备」。房主选择存档后，所有人同时进入游戏。",
                               "準備ができたら「Ready」を押してください。ホストがセーブを選ぶと全員が同時に始まります。"},
    {"lobby.ready", "Ready", "准备", "準備完了"},
    {"lobby.not_ready", "Not ready", "取消准备", "準備を取り消す"},
    {"lobby.col.ready", "Ready", "状态", "状態"},
    {"lobby.state.ready", "Ready", "已准备", "準備完了"},
    {"lobby.state.waiting", "Waiting", "等待中", "待機中"},
    {"lobby.state.host", "Host", "房主", "ホスト"},
    {"lobby.host_wait", "Waiting for every player to be ready.",
                               "全员准备好之前，「开始游戏」是锁定的。",
                               "全員の準備ができるまで「Play Game」は押せません。"},
    {"lobby.host_go", "Everyone is ready. Press Play now and pick the save.",
                               "全员已准备。点「开始游戏」并选择存档。",
                               "全員の準備ができました。「Play Game」を押してセーブを選んでください。"},
    {"lobby.client_wait", "Waiting for the host to start the game.",
                               "等待房主开始游戏。",
                               "ホストがゲームを始めるのを待っています。"},
    {"lobby.toast_host", "Co-op lobby: Play now unlocks once every player is ready.",
                               "等候室：全员准备好后「开始游戏」才会解锁（联机窗口，F5）。",
                               "待合室：全員の準備ができると「Play Game」が押せます（協力ウィンドウ、F5）。"},
    {"lobby.toast_client", "Press Ready in the Co-op lobby. The host starts the game for everyone.",
                               "打开联机窗口（F5）并点「准备」。房主会为所有人开始游戏。",
                               "協力ウィンドウ（F5）を開いて「Ready」を押してください。ホストが全員のゲームを始めます。"},
    {"lobby.all_ready", "Everyone is ready. Press Play now.",
                               "全员已准备。点「开始游戏」。",
                               "全員の準備ができました。「Play Game」を押してください。"},
    {"lobby.joining_running", "{} is already playing: joining their game", "{} 已经在游戏中：正在加入", "{} はプレイ中です：参加します"},
    {"char.placing", "Joining: travelling to the host", "加入中：正在前往房主身边", "参加中：ホストのところへ移動しています"},
    {"char.loading", "Loading your co-op character (kept by the host)", "正在载入你的联机角色（由房主保存）", "協力プレイ用キャラクターを読み込み中（ホストが保管）"},
    {"char.fresh", "Starting your co-op character (the host keeps it from now on)", "开始你的联机角色（之后由房主保存）", "協力プレイ用キャラクターを開始（以後ホストが保管）"},
    {"lobby.host_started", "{} started the game", "{} 开始了游戏", "{} がゲームを始めました"},
    // ---- party: revive, party rest, trading
    {"tab.party", "Party", "队伍",
                               "パーティー"},
    {"party.not_in_session", "Host or join a co-op room first.", "请先创建或加入联机房间",
                               "先に協力プレイのルームを作成するか参加してください"},
    {"party.help", "Out of health with a teammate nearby, you go down instead of dying: hold Interact next to a downed teammate for 3 seconds to revive them. Resting or saving at a checkpoint brings the whole party there. Trade consumables with a teammate within 7.5 m.", "有队友在附近时，生命耗尽不会死亡而是倒地：在倒地的队友身边按住交互键 3 秒即可救起。在存档点休息或存档会把全队带过去。可以与 7.5 米内的队友交易消耗品。",
                               "近くに仲間がいれば、体力が尽きても死なずにダウンします。ダウンした仲間のそばでインタラクトを3秒押し続けると蘇生できます。チェックポイントで休息やセーブをするとパーティー全員がそこへ集まります。7.5m以内の仲間と消耗品を交換できます。"},
    {"party.gift", "Shared item: {} (from {})", "共享物品：{}（来自 {}）",
                               "共有アイテム：{}（{} から）"},
    {"party.gift_many", "Got {} items found in this world before you joined", "获得了你加入前在这个世界找到的 {} 件物品",
                               "参加前にこの世界で見つかったアイテムを {} 個受け取りました"},
    {"party.mates", "Teammates", "队友",
                               "仲間"},
    {"party.no_mates", "No teammates in the room yet.", "房间里还没有队友",
                               "ルームにまだ仲間がいません"},
    {"party.state_down", "(down)", "（倒地）",
                               "（ダウン）"},
    {"party.down", "You are down. A teammate can revive you by holding Interact next to you.", "你倒地了。队友在你身边按住交互键即可救起你",
                               "ダウンしました。仲間がそばでインタラクトを押し続けると蘇生できます"},
    {"party.mate_down", "{} is down: hold Interact next to them to revive", "{} 倒地了：在其身边按住交互键救起",
                               "{} がダウン：そばでインタラクトを押し続けて蘇生"},
    {"party.being_revived", "{} is reviving you", "{} 正在救你",
                               "{} が蘇生しています"},
    {"party.revived_by", "{} revived you", "{} 救起了你",
                               "{} に蘇生されました"},
    {"party.you_revived", "You revived {}", "你救起了 {}",
                               "{} を蘇生しました"},
    {"party.label_trade", "  [hold Interact: trade]", "  [按住交互键：交易]",
                               "  [インタラクト長押し：交換]"},
    {"party.label_trade_accept", "  [hold Interact: accept trade]", "  [按住交互键：接受交易]",
                               "  [インタラクト長押し：交換を受ける]"},
    {"party.label_trading", "  [trade {}%]", "  [交易 {}%]",
                               "  [交換 {}%]"},
    {"trade.title", "Trade with {}", "与 {} 交易", "{} と交換"},
    {"trade.hud_incoming", "{} wants to trade: hold Interact next to them to accept", "{} 想和你交易：在其身边按住交互键接受",
                               "{} が交換したがっています：そばでインタラクトを長押しして受ける"},
    {"trade.hud_waiting", "Waiting for {} to accept the trade", "等待 {} 接受交易",
                               "{} が交換を受けるのを待っています"},
    {"trade.esc_hint", "Esc: cancel the trade", "Esc：取消交易", "Esc：交換をやめる"},
    {"party.hud_hold_revive", "Hold Interact: revive {}", "按住交互键：救起 {}", "インタラクト長押し：{} を蘇生"},
    {"party.hud_reviving", "Reviving {}", "正在救起 {}", "{} を蘇生中"},
    {"party.hud_being_revived", "{} is reviving you", "{} 正在救你", "{} が蘇生しています"},
    {"party.hud_down", "You are down. Wait for a teammate to revive you.", "你倒地了，等待队友救援。",
                               "ダウンしました。仲間の蘇生を待ってください。"},
    {"party.hud_hold_trade", "Hold Interact: trade with {}", "按住交互键：与 {} 交易", "インタラクト長押し：{} と交換"},
    {"party.hud_hold_accept", "Hold Interact: accept {}'s trade", "按住交互键：接受 {} 的交易", "インタラクト長押し：{} の交換を受ける"},
    {"trade.tile_help", "Click an item to offer one more, right click to take one back; hold Shift for ten.", "左键多给一个，右键收回一个；按住 Shift 一次十个。", "クリックで1つ追加、右クリックで1つ戻す。Shiftで10個ずつ。"},
    {"party.label_down", "  [down]", "  [倒地]",
                               "  [ダウン]"},
    {"party.label_hold", "  [hold Interact to revive]", "  [按住交互键救起]",
                               "  [インタラクト長押しで蘇生]"},
    {"party.label_reviving", "  [reviving {}%]", "  [救援中 {}%]",
                               "  [蘇生中 {}%]"},
    {"party.rest_sent", "The party rests with you", "全队和你一起休息",
                               "パーティー全員で休息します"},
    {"party.save_sent", "The party saves with you", "全队和你一起存档",
                               "パーティー全員でセーブします"},
    {"party.rest_follow", "{} rests at a checkpoint: the party rests there", "{} 在存档点休息：全队一起休息",
                               "{} がチェックポイントで休息：パーティーも休息します"},
    {"party.save_follow", "{} saves at a checkpoint: the party saves there", "{} 在存档点存档：全队一起存档",
                               "{} がチェックポイントでセーブ：パーティーもセーブします"},
    {"party.rest_done", "Rested with the party", "已和全队一起休息",
                               "パーティーと休息しました"},
    {"party.save_done", "Saved with the party", "已和全队一起存档",
                               "パーティーとセーブしました"},
    {"party.rest_missed_dead", "{} rested at a checkpoint, but you were dead", "{} 在存档点休息了，但你当时已死亡",
                               "{} が休息しましたが、あなたは死亡していました"},
    {"party.rest_missed_busy", "{} rested at a checkpoint while you were busy (cutscene or menu)", "{} 在存档点休息时你正忙（过场动画或菜单）",
                               "{} が休息したとき、あなたは取り込み中でした（ムービーかメニュー）"},
    {"party.rest_missed_far", "{} rested at a checkpoint you could not be taken to", "{} 休息的存档点无法传送过去",
                               "{} が休息したチェックポイントへ移動できませんでした"},
    {"trade.button", "Trade", "交易",
                               "交換"},
    {"trade.come_closer", "(come within 7.5 m)", "（需在 7.5 米内）",
                               "（7.5m以内に近づいてください）"},
    {"trade.not_down", "Nobody can trade while down", "倒地时无法交易",
                               "ダウン中は交換できません"},
    {"trade.too_far", "Stand within 7.5 m of your teammate to trade", "需要在队友 7.5 米内才能交易",
                               "交換するには仲間の7.5m以内にいてください"},
    {"trade.too_far_cancel", "Trade with {} cancelled: too far apart", "与 {} 的交易已取消：距离太远",
                               "{} との交換を中止：離れすぎました"},
    {"trade.requested", "Trade request sent to {}", "已向 {} 发送交易请求",
                               "{} に交換を申し込みました"},
    {"trade.incoming", "{} wants to trade: open the menu, Party page", "{} 想和你交易：打开菜单的「队伍」页",
                               "{} が交換を希望：メニューの「パーティー」を開いてください"},
    {"trade.waiting", "Waiting for {} to answer...", "等待 {} 回应…",
                               "{} の返事を待っています…"},
    {"trade.accept", "Accept", "接受",
                               "受ける"},
    {"trade.decline", "Decline", "拒绝",
                               "断る"},
    {"trade.cancel", "Cancel trade", "取消交易",
                               "交換をやめる"},
    {"trade.with", "Trading with {}", "正在与 {} 交易",
                               "{} と交換中"},
    {"trade.your_offer", "Your offer", "你的出价",
                               "あなたが出すもの"},
    {"trade.their_offer", "{} offers", "{} 的出价",
                               "{} が出すもの"},
    {"trade.nothing", "(nothing)", "（无）",
                               "（なし）"},
    {"trade.you", "You:", "你：",
                               "あなた："},
    {"trade.confirmed", "confirmed", "已确认",
                               "確定"},
    {"trade.not_confirmed", "not confirmed", "未确认",
                               "未確定"},
    {"trade.confirm", "Confirm trade", "确认交易",
                               "交換を確定"},
    {"trade.unconfirm", "Take back confirmation", "撤回确认",
                               "確定を取り消す"},
    {"trade.done", "Trade with {} completed", "与 {} 的交易完成",
                               "{} との交換が完了しました"},
    {"trade.failed", "The trade could not be completed: check both offers and confirm again", "交易未能完成：请检查双方出价后重新确认",
                               "交換できませんでした。内容を確認して再度確定してください"},
    {"trade.missing_items", "You no longer have everything you offered", "你已经没有出价中的全部物品",
                               "出したアイテムが足りなくなりました"},
    {"trade.declined", "{} declined the trade", "{} 拒绝了交易",
                               "{} が交換を断りました"},
    {"trade.busy", "{} is busy with another trade", "{} 正在进行另一笔交易",
                               "{} は別の交換中です"},
    {"trade.cancelled", "{} cancelled the trade", "{} 取消了交易",
                               "{} が交換をやめました"},
    {"trade.cancelled_by_you", "Trade with {} cancelled", "已取消与 {} 的交易",
                               "{} との交換をやめました"},
    {"trade.peer_left", "{} left: trade cancelled", "{} 已离开：交易取消",
                               "{} が退出したため交換を中止しました"},
    {"trade.no_answer", "{} did not answer the trade request", "{} 没有回应交易请求",
                               "{} から交換の返事がありませんでした"},
    {"summon.desc_any", "Teleport next to another player. You can move again once the place has loaded.", "传送到其他玩家身边。目的地载入后即可移动。", "ほかのプレイヤーのそばへテレポートします。読み込みが終わると動けます。"},
    {"summon.to", "Teleport to {}", "传送到 {} 身边", "{} のところへテレポート"},
    {"summon.nobody", "Nobody else is here yet.", "还没有其他玩家。", "ほかのプレイヤーはまだいません。"},
    {"fix.auto_unstuck", "You were stuck in a teleport: freed", "你卡在传送中：已解除", "テレポートで止まっていたので解除しました"},
    {"fix.unstuck", "Unstuck (cannot move or look around)", "解除卡住（不能移动或转视角）", "動けない・見回せない時に解除"},
    {"page.f5_hint", "In game, F5 opens teleport, voice chat settings and fixes", "游戏中按 F5：传送、语音设置和修复", "ゲーム中は F5 でテレポート、ボイス設定、修正"},
    {"summon.desc",            "Teleport next to the host (1.5 m to their right). You can move again once the destination has loaded.",
                               "传送到房主身边（在房主右侧 1.5 米）。传送后会等目的地区域加载完再恢复移动。",
                               "ホストの右 1.5 m にテレポートします。移動先の読み込みが終わると動けるようになります。"},
    {"summon.button",          "Teleport to the host",                              "传送到房主身边",
                               "ホストのところへテレポート"},
    {"summon.busy",            "Teleporting…",                                      "正在传送…",
                               "テレポート中…"},
    {"summon.you_host",        "You are the host. Players who joined you can teleport to you here.",
                               "你是房主。客机可以在这里传送到你身边。",
                               "あなたはホストです。参加したプレイヤーはここからあなたのところへテレポートできます。"},
    {"summon.solo",            "After joining someone's room you can teleport to the host here.",
                               "加入别人的房间后，可以在这里传送到房主身边。",
                               "ほかの人のルームに参加すると、ここからホストのところへテレポートできます。"},

    // ---- teammate model
    {"puppet.mesh_failed",     "Couldn't load the teammate model; showing a cylinder instead (see KnightsDomain.log)",
                               "化身模型加载失败，暂时用圆柱体代替（详见 KnightsDomain.log）",
                               "仲間のモデルを読み込めなかったため、代わりに円柱を表示しています（KnightsDomain.log を確認してください）"},
    {"puppet.anims_failed",    "Some teammate animations failed to load (see KnightsDomain.log)",
                               "部分化身动画加载失败（详见 KnightsDomain.log）",
                               "仲間のアニメーションの一部を読み込めませんでした（KnightsDomain.log を確認してください）"},

    // ---- About page (no store link)
    {"about.version",          "Version",                                           "版本",
                               "バージョン"},
    {"about.author",           "Author",                                            "作者",
                               "作者"},
    {"about.desc",             "A free fan-made co-op mod for Queen's Domain. Play the whole game together with up to 3 friends.",
                               "一个免费的 Queen's Domain 粉丝自制联机 MOD。最多可以和 3 位好友一起玩完整个游戏。",
                               "Queen's Domain のための無料のファンメイド協力プレイ MOD です。最大 3 人のフレンドと一緒にゲーム全体を遊べます。"},
    {"about.disclaimer",       "Not affiliated with the developer of Queen's Domain.",
                               "本 MOD 与 Queen's Domain 的开发商没有任何关联。",
                               "本 MOD は Queen's Domain の開発者とは関係ありません。"},
};
// clang-format on

struct Tables {
    std::unordered_map<std::string_view, const char*> en, zh, ja;
    Tables() {
        for (const Entry& e : kTable) {
            if (e.en) en.emplace(e.key, e.en);
            if (e.zh) zh.emplace(e.key, e.zh);
            if (e.ja) ja.emplace(e.key, e.ja);
        }
    }
};
const Tables& T() {
    static const Tables t;   // thread-safe initialisation; read-only afterwards
    return t;
}

std::atomic<Lang> g_lang{Lang::En};

const char* LangCode(Lang l) { return l == Lang::Zh ? "zh" : l == Lang::Ja ? "ja" : "en"; }

// First-run detection (game thread ticker).
gamethread::TickerId g_detectTicker = 0;
float                g_detectTimer = 0;
float                g_detectElapsed = 0;

void DetectTick(float dt) {
    g_detectTimer -= dt;
    g_detectElapsed += dt;
    if (g_detectTimer > 0) return;
    g_detectTimer = 2.0f;
    const std::string steamLang = core::SteamGameLanguage();
    if (!steamLang.empty()) {
        const Lang l = (steamLang == "schinese" || steamLang == "tchinese") ? Lang::Zh
                     : steamLang == "japanese"                                ? Lang::Ja
                                                                              : Lang::En;
        QD_INFO(kTag, "first run: Steam game language '{}' -> UI language {}", steamLang, LangCode(l));
        SetLangAndSave(l);
    } else if (g_detectElapsed < 90.0f) {
        return;   // Steam not up yet
    } else {
        QD_INFO(kTag, "first run: Steam game language unavailable after 90 s; keeping {}", LangCode(GetLang()));
    }
    gamethread::RemoveTicker(g_detectTicker);
    g_detectTicker = 0;
}

}  // namespace

const char* Tr(const char* key) {
    if (!key) return "";
    const Tables& t = T();
    const Lang lang = g_lang.load(std::memory_order_relaxed);
    if (lang == Lang::Zh) {
        if (auto it = t.zh.find(key); it != t.zh.end()) return it->second;
    } else if (lang == Lang::Ja) {
        if (auto it = t.ja.find(key); it != t.ja.end()) return it->second;
    }
    if (auto it = t.en.find(key); it != t.en.end()) return it->second;
    return key;
}

void SetLang(Lang lang) { g_lang.store(lang, std::memory_order_relaxed); }
Lang GetLang() { return g_lang.load(std::memory_order_relaxed); }

void InitI18n() {
    const auto& cfg = config::Get();
    SetLang(cfg.language == "zh" ? Lang::Zh : cfg.language == "ja" ? Lang::Ja : Lang::En);
    QD_INFO(kTag, "UI language {} ({})", LangCode(GetLang()),
            cfg.languageFromFile ? "from config.json" : "default; detecting from Steam");
    if (!cfg.languageFromFile && g_detectTicker == 0) {
        g_detectTimer = 0;
        g_detectElapsed = 0;
        g_detectTicker = gamethread::AddTicker("i18n-detect", [](float dt) { DetectTick(dt); });
    }
}

void SetLangAndSave(Lang lang) {
    SetLang(lang);
    config::SetLanguage(LangCode(lang));
    // A manual choice also ends the first-run detection.
    if (g_detectTicker) {
        gamethread::RemoveTicker(g_detectTicker);
        g_detectTicker = 0;
    }
}

}  // namespace qd::ui
