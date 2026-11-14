// coop/props/starter_suitcase.h -- a player's first join to a save brings a starter suitcase of
// their own.
//
// A new game's level holds one suitcase and seven loose items the suitcase gathers at its begin play
// (a sign frame, two MREs, two batteries, the Kel plush and a toilet roll); a spawned suitcase gathers
// nothing. The host's save has that one suitcase, so every joiner shared it. When the host answers a
// joiner with a first-join profile (coop/items/player_inventory_sync), this module notes the slot,
// and once that player stands in the world the host spawns a suitcase beside them and files the same
// seven items into it through the game's own verbs: spawnPropThroughGamemode for each item and the
// suitcase inventory's addItemAsObject. The suitcase and its contents reach the clients on the prop
// and container lanes. `net.starter_suitcase=0` turns it off. Host only.

#pragma once

#include <cstdint>

namespace coop::net {
class Session;
}  // namespace coop::net

namespace coop::starter_suitcase {

// HOST: the joiner in `slot` was answered with a first-join profile.
void NoteFirstJoin(int slot);

// HOST: the joiner in `slot` was answered with a stored profile. A suitcase still owed to the slot was a
// first-time joiner's who left before it came, and is not this player's.
void NoteReturning(int slot);

// HOST: gives each noted slot its suitcase once that player stands in the world. Game thread.
void Tick(coop::net::Session* session);

void OnDisconnect();

}  // namespace coop::starter_suitcase
