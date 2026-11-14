// coop/dev/suitcase_probe.h -- [dev] what fills the starter suitcase, and whether a spawned one fills
// itself.
//
// `suitcase_probe=1`. Twenty seconds after this peer's world is up the probe prints, for the suitcase
// class's defaults and for every suitcase of the world, the container's starterLootAsDefaultItems
// and spawnLoot members and the names of what it holds; then it spawns a suitcase in front of this
// player and prints the same for it five seconds later. Lines tagged [SUITCASE-PROBE]. The spawned
// suitcase stays in the world.

#pragma once

namespace coop::dev::suitcase_probe {

// The probe's pass. A single read when off. Game thread.
void Tick();

// Print every suitcase of the current world once, with no session: what the harness's `newgame`
// scenario calls after its level is up, where the starter suitcase still holds what the level gave it.
// Nothing when the probe is off. Game thread.
void DescribeWorld();

}  // namespace coop::dev::suitcase_probe
