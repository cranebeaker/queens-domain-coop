# Queen's Domain Co-op

Play the whole of Queen's Domain together, 2 to 4 players over Steam. The world, every boss fight, enemy attacks,
loot and story progress are shared by the party.

This is the Co-op Anything build of **[Knights' Domain](https://github.com/Lotisphere/KnightsDomain) by Mangolotis**,
the co-op mod this is built on. It is published here with Mangolotis's permission. Knights' Domain already synced
players and enemies; this build adds the rest of the game, and the same changes are offered back to Knights' Domain in
[Lotisphere/KnightsDomain#3](https://github.com/Lotisphere/KnightsDomain/pull/3). All credit for the original mod goes
to Mangolotis.

## Install

1. Download `QueensDomainCoop-<version>.zip` from [Releases](https://github.com/cranebeaker/queens-domain-coop/releases/latest).
2. Open your Queen's Domain folder (Steam: right click the game, Manage, Browse local files) and go to
   `QueensDomain\Binaries\Win64`.
3. Unzip there, so that `xinput1_3.dll` sits next to the game's exe and `KnightsDomain.dll` is in the `KnightsDomain`
   folder. Replace the files if Knights' Domain was installed before.
4. Start the game from Steam. Every player needs the same version of this build; it updates itself from this repo.

Host: Play, Co-op, create a lobby and invite friends through Steam. Press F5 in game for the co-op window.

## What is shared

- **World:** chests, doors, levers, gates, lifts, secret walls, barricades, pickups, save points, teleporters, the
  checkpoint and story counters, in every area. A player who joins gets the host's world.
- **Bosses:** every boss and miniboss wakes once for the whole party, with its arena locks, adds, attacks, rewards and
  defeat shared. Bosses pick their target by threat, so the player doing the damage draws the boss.
- **Enemies:** projectiles, area attacks and fire puddles exist for every player and hurt everyone they touch. Enemies
  near the party are split between players instead of all going for one.
- **Loot:** drops are rolled once and picked up once; pots, triggers and cutscenes play for everyone.
- **Party (F5, Party tab):** go down instead of dying when a teammate is near and get revived, rest at a checkpoint
  together, share saves, trade consumables.
- **Difficulty (Co-op tab, host):** Normal, Hard or custom enemy health and damage per extra player.

## How it is tested

Two copies of the game run side by side and a test rig reads each copy's save, actors and health:
[cranebeaker/queens-domain-coop-lab](https://github.com/cranebeaker/queens-domain-coop-lab). The 53 scenarios cover
the world, loot, every boss started by host and by client, party features, targeting and difficulty, plus a lag run
at 50 to 150 ms round trip with 2 % packet loss.

## Building

Visual Studio 2022 with the C++ workload, CMake 3.25 or newer and the Windows SDK. The build needs C++ SDK headers for
the game (Dumper-7), passed with `QD_SDK`; see the lab repo's `runner/setup.py` for the two header edits the mod needs.

```
cmake -S src -B build -G "Visual Studio 17 2022" -A x64 -DQD_SDK=<path to the SDK headers>
cmake --build build --config Release
```

`-DQD_DEV_BUILD=ON` gives the development flavour (debug console, test commands, UDP transport) the lab rig uses.

## Licence

The code is Knights' Domain's, under its own licence in [LICENSE.txt](LICENSE.txt); this build is published with
Mangolotis's permission. The libraries in `vendor` keep their own licences. Queen's Domain and its assets belong to
their owners. This mod has no connection with the developer of Queen's Domain, so please report bugs here, not to them.

Made by [Co-op Anything](https://github.com/cranebeaker).
