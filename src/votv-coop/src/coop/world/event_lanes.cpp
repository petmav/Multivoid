// coop/world/event_lanes.cpp -- see coop/world/event_lanes.h.

#include "coop/world/event_lanes.h"

#include "coop/interactables/tv_link_sync.h"
#include "coop/net/session.h"

namespace coop::event_lanes {

void Install(coop::net::Session& session) {
    coop::tv_link_sync::Install(&session);  // a web video on a television: watches on the set and its remote
}

void Tick(coop::net::Session& session, bool isHost) {
    coop::tv_link_sync::Tick();  // a link that waits for its set (returns at once when none does)
    if (!isHost) return;
}

void OnDisconnect() {
    coop::tv_link_sync::OnDisconnect();  // the parked links and each set's last address
}

}  // namespace coop::event_lanes
