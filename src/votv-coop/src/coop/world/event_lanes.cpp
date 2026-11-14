// coop/world/event_lanes.cpp -- see coop/world/event_lanes.h.

#include "coop/world/event_lanes.h"

#include "coop/net/build_share.h"
#include "coop/net/session.h"

namespace coop::event_lanes {

void Install(coop::net::Session& session) {
}

void Tick(coop::net::Session& session, bool isHost) {
    if (!isHost) return;
}

void OnDisconnect() {
    coop::net::build_share::OnDisconnect();  // a build exchange in flight is over
}

}  // namespace coop::event_lanes
