// coop/dev/pried_drill.h -- [dev] a crowbar's pry of a prop with no wall-attach component, on both peers.
//
// The pry lane (coop/props/prop_pry_sync) carries three bodies: a plank that clears its flags, a
// radiator that starts its breaking sequence, and a closed dynamite box that replaces itself. Arms
// (`pried_drill`): plank_host, plank_client, radiator_host, radiator_client -- the watching peer
// names the first stuck prop of the class in the lobby chat, the acting peer runs that prop's own
// crowbarOpen with no crowbar and says where its copy came to rest, and the watching peer judges its
// own copy: freed by the lane's replay, moved, and resting near the acting peer's. box_host,
// box_client -- the host drops a closed dynamite box beside its player and names it; the acting
// peer pries it (the client with a crowbar in its hand, so the host's judge of the asked pry
// passes), and each peer judges that its closed box is gone and one open box stands in its place.
// Every verdict is one line, `[PRIED-DRILL]` with the peer's tag and `PASS` or `FAIL`. Run the box arms with
// join_at_host=1, so the client stands within the host's reach of the box.

#pragma once

namespace coop::net {
class Session;
}  // namespace coop::net

namespace coop::dev::pried_drill {

bool IsEnabled();

// The drill's pass on either peer. A single read when off. Game thread.
void Tick(coop::net::Session* session);

// Back to the first step. Net disconnect.
void OnDisconnect();

}  // namespace coop::dev::pried_drill
