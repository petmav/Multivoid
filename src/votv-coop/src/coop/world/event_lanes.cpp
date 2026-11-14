// coop/world/event_lanes.cpp -- see coop/world/event_lanes.h.

#include "coop/world/event_lanes.h"

#include "coop/player/hunger_share.h"
#include "coop/net/session.h"

namespace coop::event_lanes {

void Install(coop::net::Session& session) {
}

void Tick(coop::net::Session& session, bool isHost) {
    coop::hunger_share::Tick(&session);  // the host's hunger rule on this peer's own player, once a second
    if (!isHost) return;
}

void OnDisconnect() {
    coop::hunger_share::OnDisconnect();  // this player's hunger rate is the game's again
}

}  // namespace coop::event_lanes
