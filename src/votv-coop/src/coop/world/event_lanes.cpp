// coop/world/event_lanes.cpp -- see coop/world/event_lanes.h.

#include "coop/world/event_lanes.h"

#include "coop/net/session.h"
#include "coop/props/fireext_thrust_sync.h"

namespace coop::event_lanes {

void Install(coop::net::Session& session) {
}

void Tick(coop::net::Session& session, bool isHost) {
    if (!isHost) return;
    coop::fireext_thrust_sync::Tick(&session);  // an extinguisher in runaway thrust (returns at once when none is)
}

void OnDisconnect() {
    coop::fireext_thrust_sync::OnDisconnect();  // the thrusting extinguishers it held
}

}  // namespace coop::event_lanes
