// coop/world/event_lanes.cpp -- see coop/world/event_lanes.h.

#include "coop/world/event_lanes.h"

#include "coop/creatures/boar_sync.h"
#include "coop/dev/perf_probe.h"
#include "coop/interactables/atv_fleet.h"
#include "coop/interactables/tv_link_sync.h"
#include "coop/net/build_share.h"
#include "coop/player/hunger_share.h"
#include "coop/net/session.h"
#include "coop/props/fireext_thrust_sync.h"
#include "coop/props/starter_suitcase.h"
#include "coop/world/agrav_sync.h"
#include "coop/world/copy_graph_gate.h"
#include "coop/world/obelisk_copy.h"
#include "coop/world/trash_fling_sync.h"
#include "ue_wrap/core/walk_timer.h"  // per-sync [WALK-TIME] attribution (diagnostic)

namespace coop::event_lanes {

void Install(coop::net::Session& session) {
    coop::trash_fling_sync::Install(&session);  // the trash prank's fling: a post watch on the trasher
    coop::tv_link_sync::Install(&session);  // a web video on a television: watches on the set and its remote
}

void Tick(coop::net::Session& session, bool isHost) {
    coop::tv_link_sync::Tick();  // a link that waits for its set (returns at once when none does)
    coop::hunger_share::Tick(&session);  // the host's hunger rule on this peer's own player, once a second
    if (!isHost) return;
    namespace PP = coop::dev::perf_probe;
    { PP::Scope _s{PP::Bucket::Interactable}; ue_wrap::ScopedWalkTimer _w{"sync:trash_fling"}; coop::trash_fling_sync::Tick(&session); }  // the props the trash prank flung
    { PP::Scope _s{PP::Bucket::Interactable}; ue_wrap::ScopedWalkTimer _w{"sync:agrav"}; coop::agrav_sync::Tick(&session); }  // the props the anti-gravity event lifts
    { PP::Scope _s{PP::Bucket::Interactable}; ue_wrap::ScopedWalkTimer _w{"sync:boar"}; coop::boar_sync::Tick(&session); }  // the gray boars the gamemode lists
    coop::fireext_thrust_sync::Tick(&session);  // an extinguisher in runaway thrust (returns at once when none is)
    coop::starter_suitcase::Tick(&session);  // a first joiner's own suitcase (returns at once when none is owed)
    coop::atv_fleet::Tick(&session);  // a second ATV for a large session (one check every five seconds)
}

void OnDisconnect() {
    coop::agrav_sync::OnDisconnect();  // the lifted props it held
    coop::trash_fling_sync::OnDisconnect();  // the fling it had noted
    coop::boar_sync::OnDisconnect();  // the counts it logged
    coop::fireext_thrust_sync::OnDisconnect();  // the thrusting extinguishers it held
    coop::copy_graph_gate::OnDisconnect();  // what a client's copies were refused
    coop::obelisk_copy::OnDisconnect();  // the spawn hook disarmed
    coop::tv_link_sync::OnDisconnect();  // the parked links and each set's last address
    coop::starter_suitcase::OnDisconnect();  // the suitcases still owed
    coop::atv_fleet::OnDisconnect();  // the session's ATV check starts over
    coop::net::build_share::OnDisconnect();  // a build exchange in flight is over
    coop::hunger_share::OnDisconnect();  // this player's hunger rate is the game's again
}

}  // namespace coop::event_lanes
