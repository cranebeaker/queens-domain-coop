// Knights' Domain — config + paths. JSON via vendor/nlohmann/json.hpp. Missing keys keep
// defaults; the file is rewritten with all keys on first run.
#pragma once
#include <string>

namespace qd::config {

struct HpScale { float normal = 0.12f, elite = 0.18f, boss = 0.25f; };   // added per extra player

// Co-op difficulty, chosen by the host and sent to everyone with the HP scale. Each value is added per extra player:
// with "hard" and two players enemies have double health and deal 25 % more damage.
//   normal: health +12 % / +18 % / +25 % (normal / elite / boss), damage unchanged
//   hard:   health +100 % for all, damage +25 %
//   custom: hp_scale and damage_scale from config.json
struct Scaling { HpScale hp; float damage = 0.0f; };
Scaling PresetScaling(const std::string& difficulty, const HpScale& customHp, float customDamage);

struct Config {
    // Dev-only keys: the release flavour neither reads nor writes transport / udp_* / profile and always runs
    // with transport "steam", profile 1. The fields stay so shared code compiles unchanged in both flavours.
    std::string transport = "steam";   // "steam" | "udp" (dev)
    int         udpPort = 27777;       // dev
    std::string udpHostIp = "127.0.0.1";   // dev
    int         profile = 1;           // dev: 2 => second instance (separate save + log + UDP client)
    std::string menuKey = "F5";
    std::string logLevel = "info";     // dev: debug|info|warn|error; release: info|warn|error
    std::string language = "en";       // "en" | "zh"
    bool        languageFromFile = false;   // config.json had a valid "language" key (else: first-run detection)
    HpScale     hpScale;                 // custom difficulty: health added per extra player
    float       damageScale = 0.25f;     // custom difficulty: enemy damage added per extra player
    std::string difficulty = "normal";   // "normal" | "hard" | "custom" (host's choice)
    std::string autosave = "both";       // 1.6.10, host: "both" (every 2 min + big moments) | "moments" | "off"
};

// Load order: <ModDir>/config.json, then (dev flavour only) command-line overrides:
//   -qdcoop-transport=udp  -qdcoop-profile=2  -qdcoop-udp-port=27777  -qdcoop-host=1.2.3.4
bool Load();
const Config& Get();
Config& Mutable();
bool Save();
// sets the UI language ("en" / "zh") in memory and persists only that key to config.json (other on-disk values
// are kept as they are; command-line overrides are never written). Does not touch a config.json that failed to parse.
// Game thread. Returns false when the file could not be written.
bool SetLanguage(const std::string& lang);
// sets the difficulty (and, for "custom", its values) in memory and persists those keys only. Game thread.
bool SetDifficulty(const std::string& difficulty, const HpScale& customHp, float customDamage);
// sets the host's autosave choice and persists that key only. Game thread.
bool SetAutosave(const std::string& mode);
// the difficulty in effect (preset values, or the custom ones)
Scaling CurrentScaling();

}  // namespace qd::config

namespace qd::paths {

const std::wstring& ModDir();      // folder containing KnightsDomain.dll
const std::wstring& GameSaved();   // %LOCALAPPDATA%\QueensDomain\Saved
// GameSaved()\KnightsDomain (dev profile >= 2: KnightsDomain\DevProfile<N>); created on demand. On first use a
// profiles.json left by the pre-1.0 QDCoop folder is copied over (never overwrites).
const std::wstring& ModSaved();
std::wstring LogFile();            // ModSaved()\KnightsDomain.log

}  // namespace qd::paths
