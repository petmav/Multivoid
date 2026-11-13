// coop/props/prop_pry_sync.h -- a crowbar's pry of a prop that carries no wall-attach component (a
// plank, a radiator, the security TV, a closed dynamite box), mirrored across peers.
//
// A prying crowbar ends in the pried object's own crowbarOpen. A wall-attach component's owner frees
// itself there through the component's unstick, which coop/props/prop_stick_sync carries, and a door's
// pry is the door lane's (coop/interactables/door_verb_intent). Every other pryable prop's body changes
// the prop it runs on -- a plank clears its flags and takes the crowbar's kick, a radiator starts its
// breaking sequence -- so the prying peer sends the prop's pose and velocity once the body has run, on
// the stick lane's message with kPropStickPried, and a receiver runs the same verb on its copy and
// lets it go from there. One class's body replaces the prop instead: a closed dynamite box spawns the
// open box and its lid and destroys itself, and a client's births do not cross, so a client's pry of
// one is cancelled and asked of the host, whose births and destroy cross as its own. docs/props.md
// carries the lane.

#pragma once

#include <cstdint>
#include <string>

namespace coop::net {
class Session;
struct PropStickStatePayload;
struct PropPryIntentPayload;
}  // namespace coop::net

namespace coop::prop_pry_sync {

// Idempotent: caches `session` and registers the script-gate watch on crowbarOpen, once.
void Install(coop::net::Session* session);

// A pried prop's state from the wire, resolved by the stick lane (prop_stick_sync::OnStickState):
// this copy runs its own crowbarOpen and is let go from the sender's pose with its velocity, unless
// another owner moves it here. A copy this lane does not own (a wall-attach component's owner, a
// prop whose pry replaces it) is left alone, said. Game thread.
void ApplyPried(void* prop, const coop::net::PropStickStatePayload& payload, const std::wstring& keyW,
                uint8_t senderPeerSlot);

// HOST: a client's PropPryIntent. Runs the prop's crowbarOpen when the prop is one whose pry replaces
// it, the sender is within reach of it and the sender's hand held a crowbar last; refused, said,
// otherwise. Game thread.
void OnPryIntent(coop::net::Session& session, const coop::net::PropPryIntentPayload& payload,
                 uint8_t senderSlot);

// Counters for the session-end line and the drills.
struct Stats {
    uint64_t sent;      // pried states this peer broadcast
    uint64_t applied;   // pried states this peer replayed on its copy
    uint64_t asked;     // CLIENT: pries cancelled here and asked of the host
    uint64_t ran;       // HOST: asked pries it ran
    uint64_t refused;   // HOST: asked pries it refused
};
Stats GetStats();

// Session end: the counters are said and cleared.
void OnDisconnect();

}  // namespace coop::prop_pry_sync
