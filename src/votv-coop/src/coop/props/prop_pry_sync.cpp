// coop/props/prop_pry_sync.cpp -- see coop/props/prop_pry_sync.h for the design.

#include "coop/props/prop_pry_sync.h"

#include "coop/element/intent_authority.h"
#include "coop/element/mirror_manager.h"
#include "coop/element/prop.h"
#include "coop/net/protocol.h"
#include "coop/net/session.h"
#include "coop/net/wire_key_util.h"
#include "coop/player/hand_item.h"  // what the sender's hand held last
#include "coop/props/prop_drive_host.h"  // OverrideClientSyncer: a replay that writes the copy ends a client's row on it
#include "coop/props/prop_element_tracker.h"
#include "coop/props/prop_key_index.h"
#include "coop/props/prop_stick_sync.h"  // the message, its fill and its let-go; the wall-attach owners it keeps
#include "ue_wrap/actors/prop.h"
#include "ue_wrap/actors/prop_events.h"  // CallCrowbarOpen, PryReplacesProp
#include "ue_wrap/core/hot_path_guard.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/script_gate.h"
#include "ue_wrap/facts/function_row.h"

#include <atomic>
#include <cstdint>
#include <string>

namespace coop::prop_pry_sync {
namespace {

namespace R  = ue_wrap::reflection;
namespace PT = coop::prop_element_tracker;
namespace sg = ue_wrap::script_gate;

// Watched by name: every class's crowbarOpen fires, so the callbacks gate on the object first.
UE_FACT_FN(kRowCrowbarOpen, "prop_pry_sync", nullptr, L"crowbarOpen", AnyClass);
constexpr int kTagPry = 0x50505259;  // 'PPRY'

// The reach an asked pry is judged against: a prying crowbar is set from the look-at trace, whose
// length is the player's arm, 200 by default. Twice that, as a door's pry is given; the authority adds
// the prop's own bounds and its allowance for the puppet's pose lagging the sender.
constexpr float kPryReachUU = 400.0f;

std::atomic<coop::net::Session*> g_session{nullptr};
bool g_watchAsked = false;

// Inside ApplyPried: the copy's own crowbarOpen is this peer taking another's pry, and is not sent
// back. Game thread, as the gate's callbacks are.
int g_replayDepth = 0;

Stats g_stats{};

// A pry this lane carries: of a prop, with no wall-attach component (those free themselves through
// the component's unstick, which the stick lane carries). A door is no prop.
bool OwnsPryOf(void* obj) {
    return obj && R::IsLive(obj) && ue_wrap::prop::IsDescendantOfProp(obj) &&
           !coop::prop_stick_sync::IsWallAttachable(obj);
}

coop::net::Session* ConnectedSession() {
    auto* s = g_session.load(std::memory_order_acquire);
    return (s && s->running() && s->connected()) ? s : nullptr;
}

// A client's pry of a prop whose body replaces it goes to the host and does not run here. A prop
// with neither key nor element id is no shared prop, and its pry runs here as in single player.
sg::Verdict OnPryPre(const sg::Call& c) {
    if (g_replayDepth > 0) return sg::Verdict::Run;
    auto* s = ConnectedSession();
    if (!s || s->role() != coop::net::Role::Client) return sg::Verdict::Run;
    if (!OwnsPryOf(c.object) || !ue_wrap::prop::PryReplacesProp(c.object)) return sg::Verdict::Run;
    coop::net::PropPryIntentPayload p{};
    const std::wstring keyW = ue_wrap::prop::GetInteractableKeyString(c.object);
    if (!keyW.empty() && keyW != L"None") coop::net::WireKeyFromString(keyW, p.key);
    const coop::element::ElementId eid = PT::GetPropElementIdForActor(c.object);
    p.elementId = (eid == coop::element::kInvalidId) ? 0u : static_cast<uint32_t>(eid);
    if (p.key.len == 0 && p.elementId == 0) return sg::Verdict::Run;
    if (!s->SendReliable(coop::net::ReliableKind::PropPryIntent, &p, sizeof(p))) {
        UE_LOGW("prop_pry_sync: the pry of key='%ls' eid=%u was not sent (the session refused it); the prop "
                "stays as the host has it", keyW.c_str(), p.elementId);
        return sg::Verdict::Cancel;
    }
    ++g_stats.asked;
    UE_LOGI("prop_pry_sync: CLIENT asked the host to pry key='%ls' eid=%u", keyW.c_str(), p.elementId);
    return sg::Verdict::Cancel;
}

// The body has run on this peer's copy: a prop still standing is sent as it now is, with the velocity
// the body left it. One the body destroyed has nothing to send, and its destroy and births cross on
// the lifecycle lanes.
void OnPryPost(const sg::Call& c) {
    if (g_replayDepth > 0) return;
    auto* s = ConnectedSession();
    if (!s || !OwnsPryOf(c.object)) return;
    coop::net::PropStickStatePayload p{};
    std::wstring keyW;
    if (!coop::prop_stick_sync::FillFreedProp(p, c.object, keyW)) return;
    p.flags = coop::net::kPropStickPried;
    s->SendReliable(coop::net::ReliableKind::PropStickState, &p, sizeof(p));
    ++g_stats.sent;
    UE_LOGI("prop_pry_sync: broadcast PRY key='%ls' eid=%u pose=(%.0f,%.0f,%.0f) angVel=(%.0f,%.0f,%.0f) deg/s",
            keyW.c_str(), p.elementId, p.locX, p.locY, p.locZ, p.angVelX, p.angVelY, p.angVelZ);
}

void* ResolveProp(const coop::net::PropPryIntentPayload& p, std::wstring& keyW) {
    keyW = coop::net::StringFromWireKey(p.key);
    void* prop = (!keyW.empty() && keyW != L"None") ? coop::prop_key_index::Find(keyW) : nullptr;
    if (prop || p.elementId == 0) return prop;
    auto* el = coop::element::MirrorManager<coop::element::Prop>::Instance().Get(
        static_cast<coop::element::ElementId>(p.elementId));
    void* a = el ? el->GetActor() : nullptr;
    return (a && R::IsLiveByIndex(a, el->GetInternalIdx())) ? a : nullptr;
}

}  // namespace

void Install(coop::net::Session* session) {
    g_session.store(session, std::memory_order_release);
    if (g_watchAsked) return;
    g_watchAsked = true;
    if (!sg::WatchName(kRowCrowbarOpen, kTagPry, &OnPryPre, &OnPryPost))
        UE_LOGE("prop_pry_sync: the script gate refused the watch on crowbarOpen -- for the rest of this "
                "process a pried plank, radiator or dynamite box changes only where it is pried");
}

void ApplyPried(void* prop, const coop::net::PropStickStatePayload& payload, const std::wstring& keyW,
                uint8_t senderPeerSlot) {
    UE_ASSERT_GAME_THREAD("prop_pry_sync::ApplyPried");
    if (!OwnsPryOf(prop) || ue_wrap::prop::PryReplacesProp(prop)) {
        UE_LOGW("prop_pry_sync: PRY for key='%ls' eid=%u names a copy this lane does not pry (a wall-attach "
                "owner, or a prop its pry replaces) -- dropped (slot %u)", keyW.c_str(), payload.elementId,
                senderPeerSlot);
        return;
    }
    // The replay writes the copy's flags, pose and physics, so a client that syncs the prop loses it
    // first, as to a host verb. A no-op off the host and for a prop with no client row.
    coop::prop_drive_host::OverrideClientSyncer(prop, "pry replay");
    ++g_replayDepth;
    const bool ran = ue_wrap::prop::CallCrowbarOpen(prop);
    --g_replayDepth;
    if (!ran) {
        UE_LOGW("prop_pry_sync: PRY key='%ls' eid=%u -- the copy's crowbarOpen did not run here; the copy stays "
                "as it is", keyW.c_str(), payload.elementId);
        return;
    }
    ++g_stats.applied;
    const bool letGo = R::IsLive(prop) && coop::prop_stick_sync::LetGoFrom(prop, payload);
    UE_LOGI("prop_pry_sync: PRY applied key='%ls' eid=%u (%s; slot %u)", keyW.c_str(), payload.elementId,
            letGo ? "let go from the sender's pose" : "another hold moves it", senderPeerSlot);
}

void OnPryIntent(coop::net::Session& session, const coop::net::PropPryIntentPayload& payload,
                 uint8_t senderSlot) {
    UE_ASSERT_GAME_THREAD("prop_pry_sync::OnPryIntent");
    if (session.role() != coop::net::Role::Host) return;
    std::wstring keyW;
    void* prop = ResolveProp(payload, keyW);
    const char* why = nullptr;
    coop::element::IntentSubject subject{};
    if (!prop) {
        why = "no live prop by that key or id";
    } else if (!OwnsPryOf(prop) || !ue_wrap::prop::PryReplacesProp(prop)) {
        why = "not a prop whose pry replaces it";
    } else {
        subject = coop::element::IntentTarget::ForClientIntent(session, senderSlot, kPryReachUU).Authorize(prop);
        if (!subject) why = coop::element::OutcomeName(subject.outcome);
        // The prying crowbar destroys the held one as it is set, so a pry ends with the hand empty.
        else if (!coop::hand_item::LastHeldClassIs(senderSlot, L"prop_crowbar_C"))
            why = "the sender's hand did not hold a crowbar last";
    }
    if (why) {
        ++g_stats.refused;
        UE_LOGI("prop_pry_sync: DENY slot=%u pry key='%ls' eid=%u -- %s (%.0f uu of %.0f)",
                static_cast<unsigned>(senderSlot), keyW.c_str(), payload.elementId, why, subject.distUU,
                subject.reachUU);
        return;
    }
    const bool ran = ue_wrap::prop::CallCrowbarOpen(prop);
    if (ran) ++g_stats.ran; else ++g_stats.refused;
    UE_LOGI("prop_pry_sync: host ran slot %u's pry on key='%ls' eid=%u: dispatched=%d, the prop %s",
            static_cast<unsigned>(senderSlot), keyW.c_str(), payload.elementId, ran ? 1 : 0,
            R::IsLive(prop) ? "still stands" : "is replaced");
}

Stats GetStats() { return g_stats; }

void OnDisconnect() {
    if (g_stats.sent || g_stats.applied || g_stats.asked || g_stats.ran || g_stats.refused)
        UE_LOGI("prop_pry_sync: session end -- sent=%llu applied=%llu asked=%llu ran=%llu refused=%llu",
                static_cast<unsigned long long>(g_stats.sent), static_cast<unsigned long long>(g_stats.applied),
                static_cast<unsigned long long>(g_stats.asked), static_cast<unsigned long long>(g_stats.ran),
                static_cast<unsigned long long>(g_stats.refused));
    g_stats = Stats{};
}

}  // namespace coop::prop_pry_sync
