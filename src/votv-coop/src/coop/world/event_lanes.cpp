// coop/world/event_lanes.cpp -- see coop/world/event_lanes.h.

#include "coop/world/event_lanes.h"

#include "coop/interactables/atv_fleet.h"
#include "coop/net/session.h"
#include "coop/props/starter_suitcase.h"

namespace coop::event_lanes {

void Install(coop::net::Session& session) {
}

void Tick(coop::net::Session& session, bool isHost) {
    if (!isHost) return;
    coop::starter_suitcase::Tick(&session);  // a first joiner's own suitcase (returns at once when none is owed)
    coop::atv_fleet::Tick(&session);  // a second ATV for a large session (one check every five seconds)
}

void OnDisconnect() {
    coop::starter_suitcase::OnDisconnect();  // the suitcases still owed
    coop::atv_fleet::OnDisconnect();  // the session's ATV check starts over
}

}  // namespace coop::event_lanes
