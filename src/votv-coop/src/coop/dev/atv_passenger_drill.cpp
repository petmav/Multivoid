// coop/dev/atv_passenger_drill.cpp -- see coop/dev/atv_passenger_drill.h.

#include "coop/dev/atv_passenger_drill.h"

#include "atv_seat.h"        // TrySit (co-located private header)
#include "atv_spawn_near.h"  // SpawnAtNav, KeyOf (co-located private header)

#include "coop/config/config.h"
#include "coop/config/config_registry.h"
#include "coop/cheats/self_teleport.h"
#include "coop/dev/director/clear_hand.h"
#include "coop/dev/director/director.h"
#include "coop/dev/drill_chat.h"
#include "coop/dev/set_clock.h"
#include "coop/interactables/atv_authority.h"
#include "coop/interactables/atv_passenger.h"
#include "coop/interactables/atv_sync.h"
#include "coop/net/protocol.h"
#include "coop/net/session.h"
#include "coop/net/wire_key_util.h"
#include "coop/player/players_registry.h"
#include "coop/player/remote_player.h"
#include "coop/session/net_pump.h"
#include "harness/screenshot.h"

#include "ue_wrap/core/call.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/reflection_props.h"
#include "ue_wrap/core/sdk_profile.h"
#include "ue_wrap/core/trace.h"
#include "ue_wrap/core/types.h"
#include "ue_wrap/devices/atv.h"
#include "ue_wrap/devices/atv_passenger.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/engine/engine_attach.h"
#include "ue_wrap/engine/engine_component.h"
#include "ue_wrap/engine/engine_mainplayer.h"
#include "ue_wrap/engine/engine_nav.h"
#include "ue_wrap/engine/engine_pawn.h"

#include <windows.h>

#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace coop::dev::atv_passenger_drill {
namespace {

namespace A  = ue_wrap::atv;
namespace AP = ue_wrap::atv_passenger;
namespace AU = coop::atv_authority;
namespace DC = coop::dev::drill_chat;
namespace E  = ue_wrap::engine;
namespace P  = ue_wrap::profile;
namespace R  = ue_wrap::reflection;
namespace SN = coop::dev::atv_spawn_near;
using ue_wrap::FRotator;
using ue_wrap::FVector;
using coop::net::AtvPassengerRefusal;

constexpr const char* kTag = "[ATVPASS-DRILL]";
constexpr uint64_t kBoundMs = 240000;        // a peer waiting on the other's line or the world's edge
constexpr uint64_t kShortMs = 20000;         // an effect that must land on a tick or two
constexpr float kAheadCm = 600.f;            // the ATV is born this far in front of the host's player
constexpr float kWalkReachCm = 330.f;        // the client's walk ends this close to the ATV's root
constexpr int   kWalkDeadlineS = 300;
constexpr float kDriveCm = 800.f;            // the host drives this far
constexpr float kRideTolCm = 60.f;           // a rider's body against its seat on the machine's own copy, at any moment of the ride
constexpr float kFollowFrac = 0.7f;          // the client's body must have moved this share of the drive
constexpr float kParkedCmS = 5.f;
constexpr float kAwayCm = 5000.f;            // the out-of-reach arm moves the host's ATV this far
constexpr uint64_t kBucketGapMs = 1600;      // the host's board bucket refills 1 a second: red requests are spaced past it
constexpr uint8_t kRawSeqBase = 200;         // a raw request's seq: never the client's own pending one

bool Enabled() {
    static const bool on = coop::config::ResolveEnum(::coop::config_registry::rows::atv_passenger_drill) == "on";
    return on;
}

uint64_t Now() { return ::GetTickCount64(); }

float Dist(const FVector& a, const FVector& b) {
    const float dx = a.X - b.X, dy = a.Y - b.Y, dz = a.Z - b.Z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

float HorizDist(const FVector& a, const FVector& b) {
    const float dx = a.X - b.X, dy = a.Y - b.Y;
    return std::sqrt(dx * dx + dy * dy);
}

std::string Num(float v) { char b[32]; std::snprintf(b, sizeof(b), "%.0f", v); return b; }

// ---- the shared clock and lines ---------------------------------------------------------------------------

struct Shared {
    int      step = 0;
    uint64_t stepAt = 0;
    bool     done = false;
    bool     flag = false;      // a one-shot inside a step
    bool     logged = false;    // a line said once while a step waits
    uint64_t flagAt = 0;
};

void Next(Shared& g) { ++g.step; g.stepAt = Now(); g.flag = false; g.logged = false; }
bool Timed(const Shared& g, uint64_t bound) { return Now() - g.stepAt > bound; }
bool Seen(const char* line) { return DC::SeenHas(line); }

void Pass(Shared& g, const char* role, const std::string& why) {
    UE_LOGI("%s %s PASS -- %s", kTag, role, why.c_str());
    g.done = true;
}
void Fail(Shared& g, const char* role, const std::string& why) {
    UE_LOGE("%s %s FAIL -- %s", kTag, role, why.c_str());
    DC::Say(std::string("atvpass ") + role + " FAIL");
    g.done = true;
}

// The ATV the lane's table holds under `key` on this machine, and its root.
bool AtvOf(const std::wstring& key, void** atv, FVector* root = nullptr) {
    coop::atv_sync::TableView t;
    if (!coop::atv_sync::ReadTable(key, t) || !t.actor) return false;
    *atv = t.actor;
    FRotator rot{};
    return !root || A::GetRootTransform(t.actor, *root, rot);
}

coop::atv_sync::TableView TableOf(const std::wstring& key) {
    coop::atv_sync::TableView t;
    coop::atv_sync::ReadTable(key, t);
    return t;
}

// The geometry the seat offsets are measured from: the rig's bounds, its seat and cargo components and the player's
// capsule, each in the rig's own axes (X forward, Y right, Z up) from its root.
void LogGeometry(void* atv, void* player) {
    FVector root{};
    FRotator rot{};
    if (!A::GetRootTransform(atv, root, rot)) return;
    const FVector f = E::GetActorForwardVector(atv), u = E::GetActorUpVector(atv);
    const FVector r{u.Y * f.Z - u.Z * f.Y, u.Z * f.X - u.X * f.Z, u.X * f.Y - u.Y * f.X};   // up x forward: the right axis
    auto local = [&](const FVector& w) {
        const FVector d{w.X - root.X, w.Y - root.Y, w.Z - root.Z};
        return FVector{d.X * f.X + d.Y * f.Y + d.Z * f.Z, d.X * r.X + d.Y * r.Y + d.Z * r.Z, d.X * u.X + d.Y * u.Y + d.Z * u.Z};
    };
    const FVector sc = E::GetActorScale3D(atv);
    UE_LOGI("%s geom: root (%.0f,%.0f,%.0f) rot (%.1f,%.1f,%.1f) scale (%.2f,%.2f,%.2f)", kTag, root.X, root.Y, root.Z, rot.Pitch,
            rot.Yaw, rot.Roll, sc.X, sc.Y, sc.Z);
    for (const bool colliding : {false, true}) {
        FVector o{}, ext{};
        if (E::GetActorBounds(atv, colliding, o, ext)) {
            const FVector lo = local(o);
            UE_LOGI("%s geom: bounds(%s) origin local (%.0f,%.0f,%.0f) extent world (%.0f,%.0f,%.0f)", kTag,
                    colliding ? "colliding" : "all", lo.X, lo.Y, lo.Z, ext.X, ext.Y, ext.Z);
        }
    }
    void* cls = R::ClassOf(atv);
    for (const wchar_t* name : {L"sitarrow", L"sitkerf", L"playermodel", L"boundingBox", L"hitbox", L"cam", L"cameraRoot",
                                L"overlapBox", L"collision", L"lookatBox", L"Box", L"Box1", L"Scene", L"Arrow", L"Arrow1", L"Arrow2",
                                L"mesh_playerVisible"}) {
        const int32_t off = cls ? R::FindPropertyOffset(cls, name) : -1;
        void* comp = off >= 0 ? *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(atv) + off) : nullptr;
        FVector w{};
        if (!comp || !R::IsLive(comp) || !E::TryGetComponentLocation(comp, w)) {
            UE_LOGI("%s geom: component %ls -- %s", kTag, name, off < 0 ? "no such property" : "not live");
            continue;
        }
        const FVector lw = local(w);
        const FVector rel = E::GetComponentRelativeLocation(comp);
        UE_LOGI("%s geom: component %ls (%ls) local (%.0f,%.0f,%.0f) relative (%.0f,%.0f,%.0f)", kTag, name,
                R::ClassNameOf(comp).c_str(), lw.X, lw.Y, lw.Z, rel.X, rel.Y, rel.Z);
    }
    // Every primitive component the rig owns, with its bounds in the rig's own axes: the body mesh is the rack's measure.
    void* comps[48];
    const int n = E::OwnPrimitiveComponents(atv, comps, 48);
    for (int i = 0; i < n; ++i) {
        void* comp = comps[i];
        const int32_t boff = R::FindPropertyOffset(R::ClassOf(comp), L"Bounds");
        if (boff < 0) continue;
        const float* b = reinterpret_cast<const float*>(reinterpret_cast<const uint8_t*>(comp) + boff);   // FBoxSphereBounds: origin, extent, radius
        const FVector lo = local({b[0], b[1], b[2]});
        UE_LOGI("%s geom: primitive %ls (%ls) bounds origin local (%.0f,%.0f,%.0f) extent (%.0f,%.0f,%.0f)", kTag,
                R::ToString(R::NameOf(comp)).c_str(), R::ClassNameOf(comp).c_str(), lo.X, lo.Y, lo.Z, b[3], b[4], b[5]);
    }
    if (player) {
        FVector at{};
        E::TryGetActorLocation(player, at);
        UE_LOGI("%s geom: the player's capsule half height %.0f radius %.0f, at (%.0f,%.0f,%.0f)", kTag,
                E::GetActorCharacterHalfHeight(player), E::GetActorCharacterRadius(player), at.X, at.Y, at.Z);
    }
    // The rig's own surface under each place a body is put: a trace straight down through the place, from above the rig, that
    // may hit the rig itself (only the player is ignored). A seat's feet rest at its centre minus the capsule's half height.
    if (player) {
        const uint8_t types[4] = {0, 1, 3, 4};   // WorldStatic, WorldDynamic, PhysicsBody, Vehicle
        void* const ignore[1] = {player};
        const FVector places[4] = {{-25.f, 0.f, 0.f}, AP::SeatOffset(0), AP::SeatOffset(1), AP::SeatOffset(2)};
        const char* names[4] = {"the driver's place", "seat 0", "seat 1", "seat 2"};
        for (int i = 0; i < 4; ++i) {
            const FVector top{root.X + f.X * places[i].X + r.X * places[i].Y + u.X * 300.f, root.Y + f.Y * places[i].X + r.Y * places[i].Y + u.Y * 300.f,
                              root.Z + f.Z * places[i].X + r.Z * places[i].Y + u.Z * 300.f};
            const FVector bottom{top.X - u.X * 500.f, top.Y - u.Y * 500.f, top.Z - u.Z * 500.f};
            ue_wrap::trace::Hit hit;
            if (ue_wrap::trace::LineHitForObjects(player, top, bottom, types, 4, ignore, 1, &hit) && hit.blocked) {
                const FVector lh = local(hit.point);
                UE_LOGI("%s geom: %s: the surface under it is at local z %.0f (hit %ls, %s the rig), a rider's centre sits %.0f above it", kTag, names[i], lh.Z,
                        hit.actor ? R::ClassNameOf(hit.actor).c_str() : L"nothing", hit.actor == atv ? "is" : "is not", places[i].Z - lh.Z);
            } else {
                UE_LOGI("%s geom: %s: the trace down found no surface", kTag, names[i]);
            }
        }
    }
    // The rig's surface over a grid behind the driver (local z of the first hit; -- is none), a row per X, the columns Y -60 to 60 by 20.
    if (player) {
        const uint8_t types[4] = {0, 1, 3, 4};
        void* const ignore[1] = {player};
        for (const float gx : {-40.f, -60.f, -80.f, -100.f, -120.f, -140.f}) {
            char row[160] = {};
            size_t used = 0;
            for (float gy = -60.f; gy <= 60.f; gy += 20.f) {
                const FVector top{root.X + f.X * gx + r.X * gy + u.X * 300.f, root.Y + f.Y * gx + r.Y * gy + u.Y * 300.f, root.Z + f.Z * gx + r.Z * gy + u.Z * 300.f};
                const FVector bottom{top.X - u.X * 500.f, top.Y - u.Y * 500.f, top.Z - u.Z * 500.f};
                ue_wrap::trace::Hit hit;
                char cell[16];
                if (ue_wrap::trace::LineHitForObjects(player, top, bottom, types, 4, ignore, 1, &hit) && hit.blocked && hit.actor == atv)
                    std::snprintf(cell, sizeof(cell), "%4.0f", local(hit.point).Z);
                else
                    std::snprintf(cell, sizeof(cell), "  --");
                used += static_cast<size_t>(std::snprintf(row + used, sizeof(row) - used, "%s ", cell));
            }
            UE_LOGI("%s geom: surface grid x=%4.0f, y -60..60: %s", kTag, gx, row);
        }
    }
    for (int i = 0; i < AP::kSeats; ++i) {
        FVector w{};
        if (AP::SeatWorldLocation(atv, i, w)) {
            const FVector lw = local(w);
            UE_LOGI("%s geom: seat %d offset (%.0f,%.0f,%.0f) -> world (%.0f,%.0f,%.0f), local back (%.0f,%.0f,%.0f)", kTag, i,
                    AP::SeatOffset(i).X, AP::SeatOffset(i).Y, AP::SeatOffset(i).Z, w.X, w.Y, w.Z, lw.X, lw.Y, lw.Z);
        }
    }
}

// Where the driver's body is while it sits: the player's actor and its camera, in the rig's own axes from its root.
void LogDriverSeat(void* atv, void* player) {
    FVector root{}, at{};
    FRotator rot{};
    if (!A::GetRootTransform(atv, root, rot) || !E::TryGetActorLocation(player, at)) return;
    const FVector f = E::GetActorForwardVector(atv), u = E::GetActorUpVector(atv);
    const FVector r{u.Y * f.Z - u.Z * f.Y, u.Z * f.X - u.X * f.Z, u.X * f.Y - u.Y * f.X};
    auto local = [&](const FVector& w) {
        const FVector d{w.X - root.X, w.Y - root.Y, w.Z - root.Z};
        return FVector{d.X * f.X + d.Y * f.Y + d.Z * f.Z, d.X * r.X + d.Y * r.Y + d.Z * r.Z, d.X * u.X + d.Y * u.Y + d.Z * u.Z};
    };
    const FVector la = local(at), lc = local(E::GetCameraLocation());
    UE_LOGI("%s geom: the seated driver's actor local (%.0f,%.0f,%.0f), its camera local (%.0f,%.0f,%.0f)", kTag, la.X, la.Y, la.Z, lc.X,
            lc.Y, lc.Z);
}

// The use key, at the real recognition path: the aim set at `aimAt` (null: nothing), then InpActEvt_use dispatched on the player.
bool PressUse(void* player, void* aimAt) {
    void* cls = R::FindClass(P::name::MainPlayerClass);
    void* fn = cls ? R::FindFunction(cls, P::name::MainPlayerUseInputEventFn) : nullptr;
    if (!fn) return false;
    E::WriteMainPlayerLookAtActor(player, aimAt);
    const int32_t fs = R::FunctionFrameSize(fn);
    std::vector<uint8_t> frame(fs > 0 ? static_cast<size_t>(fs) : 0, 0u);
    return R::CallFunction(player, fn, frame.empty() ? nullptr : frame.data());
}

// A body that has just left a seat, judged: beside the ATV (the rig may roll on without its driver, so it is measured at the moment of the leave
// and not later) and on the ground; then, a moment on, it must not have fallen or been pushed.
struct LeaveCheck {
    uint64_t atMs = 0;
    FVector  at{};
    float    horiz = 0.f, up = 0.f;
    bool     floorAtLeave = false;
    std::string why;
};

// 0 while it waits, 1 when the body stood, -1 when it did not (`c.why`).
int CheckLeft(LeaveCheck& c, void* player, void* atv) {
    FVector root{}, at{};
    FRotator rot{};
    if (!A::GetRootTransform(atv, root, rot) || !E::TryGetActorLocation(player, at)) { c.why = "the ATV or the body does not read after the leave"; return -1; }
    E::FloorRead fr;
    const bool floorNow = E::ReadCurrentFloor(player, &fr) && fr.blocking && fr.walkable;
    if (c.atMs == 0) {
        c.atMs = Now();
        c.at = at;
        c.horiz = HorizDist(at, root);
        c.up = at.Z - root.Z;
        c.floorAtLeave = floorNow;
        return 0;
    }
    if (Now() - c.atMs < 1800) return 0;
    const float dz = at.Z - c.at.Z, slid = HorizDist(at, c.at);
    if (c.horiz >= 100.f && c.horiz <= 450.f && std::fabs(c.up) < 200.f && std::fabs(dz) < 40.f && slid < 60.f && floorNow) return 1;
    c.why = "after the leave the body was " + Num(c.horiz) + " cm from the ATV and " + Num(c.up) + " cm above its root; a moment later it had moved " + Num(dz) +
            " cm in height and " + Num(slid) + " across, floor under it: " + std::to_string(floorNow ? 1 : 0);
    return -1;
}

// ---- the host ---------------------------------------------------------------------------------------------------

enum class H : int {
    WaitClient, Spawn, Park, Sit, WaitBoard, Judge1, DriveStart, Drive, DriverLeaves, WaitStayed, WaitLeft, Judge2,
    ClientDrives, WaitClientSat, HostBoard, HostRideWait, HostLeave, ClientUndrove,
    Sit2, WaitBoard2, Judge3,
    RedDupGo, RedDupWait, RedLeave, RedLeaveWait, FullArm, FullWait, ReachArm, ReachWait, Finish
};

struct Host {
    Shared                g;
    void*                 atv = nullptr;
    std::wstring          key;
    uint64_t              spawnAt = 0;
    FVector               lastAt{};
    uint64_t              lastAtMs = 0;
    int                   calm = 0;
    bool                  spawned = false;
    atv_seat::SitTry      sit;
    FVector               driveFrom{};
    float                 maxPuppetErr = 0.f;
    float                 clientRideErr = -1.f;
    int                   inputOff = -1;
    uint8_t               inputMask = 0;
    void*                 atvClass = nullptr;
    AU::Counters          base{};
    uint64_t              redAt = 0;
    coop::director::ClearHand clear;   // the sit verb wants empty hands: the save's shovel is dropped first
    int                   presses = 0;
    uint64_t              pressAt = 0;
    LeaveCheck            leave;
    bool                  cleared = false;
    uint64_t              lookStartMs = 0, lookAimedMs = 0;   // the host's look at the seat after the driver left
    bool                  lookDone = false, lookAimed = false;
    int                   lookShot = 0;
};
Host gh;

// The throttle: ATV_C::input_forward, the field the game's own key handler writes.
void Throttle(void* atv, bool on) {
    if (gh.inputOff < 0) {
        gh.atvClass = R::ClassOf(atv);
        if (!gh.atvClass || !R::FindBoolProperty(gh.atvClass, L"input_forward", gh.inputOff, gh.inputMask)) {
            gh.inputOff = -1;
            return;
        }
    }
    uint8_t* b = reinterpret_cast<uint8_t*>(atv) + gh.inputOff;
    if (on) *b |= gh.inputMask; else *b &= static_cast<uint8_t>(~gh.inputMask);
}

// The handbrake: ATV_C::brake and the game's own setBrake, which applies it to the rig.
void SetBrake(void* atv, bool on) {
    int32_t off = -1;
    uint8_t mask = 0;
    void* cls = R::ClassOf(atv);
    if (!cls || !R::FindBoolProperty(cls, L"brake", off, mask)) return;
    uint8_t* b = reinterpret_cast<uint8_t*>(atv) + off;
    if (((*b & mask) != 0) == on) return;
    if (on) *b |= mask; else *b &= static_cast<uint8_t>(~mask);
    if (void* fn = R::FindFunction(cls, L"setBrake")) {
        ue_wrap::ParamFrame pf(fn);
        if (pf.valid()) ue_wrap::Call(atv, pf);
    }
}

bool HostLiveAtv() {
    if (gh.atv && AtvOf(gh.key, &gh.atv)) return true;
    return false;
}

void HostTick(coop::net::Session* s) {
    Shared& g = gh.g;
    if (Seen("atvpass client FAIL")) { Fail(g, "host", "the client reported a failed arm (its own line has why)"); return; }
    void* me = coop::players::Registry::Get().Local();
    switch (static_cast<H>(g.step)) {
    case H::WaitClient:
        if (!s->IsSlotWorldReady(1) || !me) { if (g.stepAt == 0) g.stepAt = Now(); return; }
        if (Seen("atvpass client-ready")) { DC::Say("atvpass host-ready"); Next(g); }
        else if (Timed(g, kBoundMs)) Fail(g, "host", "the client never said it was ready");
        return;

    case H::Spawn: {
        if (!gh.spawned) {
            // Noon, so the screenshots of the seats show the rig and not a night sky.
            int hour = 0, minute = 0, day = 1;
            float sun = 0.f;
            if (coop::dev::set_clock::ReadCurrent(hour, minute, day, sun)) coop::dev::set_clock::SetClock(day, 12, 0);
            FVector at{};
            if (!E::TryGetActorLocation(me, at)) { Fail(g, "host", "the host's player does not read"); return; }
            const FVector fwd = E::GetActorForwardVector(me);
            SN::Spawned sp;
            gh.spawnAt = Now();
            if (!SN::SpawnAtNav(me, {at.X + fwd.X * kAheadCm, at.Y + fwd.Y * kAheadCm, at.Z + SN::kLiftCm}, sp)) {
                Fail(g, "host", std::string(sp.what) + ": " + sp.got);
                return;
            }
            gh.atv = sp.actor;
            gh.spawned = true;
        }
        const char* why = nullptr;
        const SN::Key k = SN::KeyOf(gh.atv, &gh.key, &why);
        if (k == SN::Key::Bad) { Fail(g, "host", std::string("the ATV's key cannot ride a chat line: ") + why); return; }
        if (k == SN::Key::Ok) {
            DC::Say("atvpass key " + std::string(gh.key.begin(), gh.key.end()));
            UE_LOGI("%s host: the spawned ATV is '%ls'", kTag, gh.key.c_str());
            Next(g);
        } else if (Timed(g, kShortMs * 3)) {
            Fail(g, "host", "the spawned ATV never entered the host's table");
        }
        return;
    }

    case H::Park: {
        FVector at{};
        if (!HostLiveAtv() || !E::TryGetActorLocation(gh.atv, at)) { Fail(g, "host", "the spawned ATV is gone"); return; }
        const uint64_t now = Now();
        if (gh.lastAtMs == 0 || now - gh.lastAtMs >= 250) {
            if (gh.lastAtMs != 0) {
                const float secs = static_cast<float>(now - gh.lastAtMs) / 1000.f;
                gh.calm = Dist(at, gh.lastAt) / secs < kParkedCmS ? gh.calm + 1 : 0;
            }
            gh.lastAt = at;
            gh.lastAtMs = now;
        }
        // Parked, and the client's body is on this machine (a board before its first pose is refused no_body).
        if (gh.calm >= 2 && coop::players::Registry::Get().LifeOf(1) == coop::players::PuppetLife::Alive) {
            LogGeometry(gh.atv, me);
            DC::Say("atvpass parked");
            Next(g);
        } else if (Timed(g, kShortMs * 3)) {
            Fail(g, "host", "the spawned ATV never came to rest");
        }
        return;
    }

    case H::Sit:
    case H::Sit2:
        if (!HostLiveAtv()) { Fail(g, "host", "the ATV is gone before the sit"); return; }
        if (!gh.cleared) {
            const coop::director::ClearState cs = gh.clear.Tick();
            if (cs == coop::director::ClearState::Failed) { Fail(g, "host", std::string("the host's hand could not be cleared: ") + gh.clear.Why()); return; }
            if (cs == coop::director::ClearState::Working) { if (Timed(g, kShortMs * 3)) Fail(g, "host", "the host's hand never cleared"); return; }
            gh.cleared = true;
            g.stepAt = Now();
        }
        if (!g.flag) { gh.sit = atv_seat::SitTry{}; g.flag = true; }
        if (!atv_seat::TrySit(me, gh.atv, gh.sit)) {
            if (Timed(g, kShortMs * 3)) Fail(g, "host", "the host's sit never finished");
            return;
        }
        if (coop::atv_sync::ActorSeatedBy(0) != gh.atv) { Fail(g, "host", "the host's sit did not take"); return; }
        DC::Say(static_cast<H>(g.step) == H::Sit ? "atvpass host-sat" : "atvpass host-sat2");
        UE_LOGI("%s host: sat as the driver", kTag);
        LogDriverSeat(gh.atv, me);
        Next(g);
        return;

    case H::WaitBoard:
    case H::WaitBoard2: {
        const bool second = static_cast<H>(g.step) == H::WaitBoard2;
        if (Seen(second ? "atvpass client-boarded2" : "atvpass client-boarded")) { gh.base = AU::ReadCounters(); Next(g); }
        else if (Timed(g, kBoundMs)) Fail(g, "host", "the client never boarded");
        return;
    }

    case H::Judge1:
    case H::Judge3: {
        // The host's own table holds the client in seat 0, the host's copy of the rig carries its puppet at the seat.
        if (!HostLiveAtv()) { Fail(g, "host", "the ATV is gone"); return; }
        bool attached = false;
        const int seat = coop::atv_passenger::SeatOfSlot(gh.key, 1, &attached);
        const coop::atv_sync::TableView t = TableOf(gh.key);
        FVector seatAt{}, puppetAt{};
        coop::RemotePlayer* rp = coop::players::Registry::Get().Puppet(1);
        const bool placed = AP::SeatWorldLocation(gh.atv, 0, seatAt) && rp && rp->valid() && rp->TryGetLocation(puppetAt);
        if (seat != 0 || !attached || !placed || t.passengerSlot[0] != 1) {
            if (Timed(g, kShortMs)) {
                Fail(g, "host", "the client's seat: table seat " + std::to_string(seat) + ", puppet attached " + std::to_string(attached ? 1 : 0) +
                                    ", puppet read " + std::to_string(placed ? 1 : 0));
            }
            return;
        }
        const float err = Dist(puppetAt, seatAt);
        if (err > kRideTolCm) {
            if (Timed(g, kShortMs)) Fail(g, "host", "the client's puppet is " + Num(err) + " cm from seat 0 (tolerance " + Num(kRideTolCm) + ")");
            return;
        }
        if (!g.logged) {
            g.logged = true;
            UE_LOGI("%s host: the client holds passenger seat 0, its puppet sits %.0f cm from it on the host's copy, speed shown seated=%d",
                    kTag, err, rp->IsSeated() ? 1 : 0);
        }
        if (!rp->IsSeated()) { Fail(g, "host", "the seated puppet still shows the stream's locomotion"); return; }
        // The look of the seat, for a person: the host's view (the driver's chase camera) of the rig with its rider on it.
        if (static_cast<H>(g.step) == H::Judge1) {
            if (!g.flag) { g.flag = true; g.flagAt = Now(); return; }
            if (Now() - g.flagAt < 1500) return;
            harness::screenshot::Capture(L"atvpass-rest");
        }
        DC::Say(static_cast<H>(g.step) == H::Judge1 ? "atvpass host-sees-rider" : "atvpass host-sees-rider2");
        Next(g);
        return;
    }

    case H::DriveStart:
        if (!Seen("atvpass client-armed")) { if (Timed(g, kBoundMs)) Fail(g, "host", "the client never armed its ride samples"); return; }
        if (!HostLiveAtv() || !AtvOf(gh.key, &gh.atv, &gh.driveFrom)) { Fail(g, "host", "the ATV is gone before the drive"); return; }
        SetBrake(gh.atv, false);
        Throttle(gh.atv, true);
        gh.maxPuppetErr = 0.f;
        DC::Say("atvpass drive-start");
        UE_LOGI("%s host: driving from (%.0f,%.0f,%.0f)", kTag, gh.driveFrom.X, gh.driveFrom.Y, gh.driveFrom.Z);
        Next(g);
        return;

    case H::Drive: {
        FVector root{}, seatAt{}, puppetAt{};
        if (!HostLiveAtv() || !AtvOf(gh.key, &gh.atv, &root)) { Fail(g, "host", "the ATV is gone during the drive"); return; }
        coop::RemotePlayer* rp = coop::players::Registry::Get().Puppet(1);
        if (rp && rp->valid() && rp->TryGetLocation(puppetAt) && AP::SeatWorldLocation(gh.atv, 0, seatAt)) {
            const float err = Dist(puppetAt, seatAt);
            if (err > gh.maxPuppetErr) gh.maxPuppetErr = err;
        }
        const float moved = Dist(root, gh.driveFrom);
        // A rig that will not take the throttle is pushed along its own forward axis: the drill measures the ride, not the engine.
        if (Now() - g.stepAt > 5000 && moved < 100.f) {
            const FVector fwd = E::GetActorForwardVector(gh.atv);
            E::SetActorRootPhysicsVelocity(gh.atv, {fwd.X * 500.f, fwd.Y * 500.f, 0.f}, {});
        }
        if (moved >= kDriveCm * 0.5f && !g.flag) { g.flag = true; harness::screenshot::Capture(L"atvpass-drive"); }
        if (moved >= kDriveCm) {
            Throttle(gh.atv, false);
            UE_LOGI("%s host: drove %.0f cm; the client's puppet stayed within %.0f cm of its seat (tolerance %.0f)", kTag, moved,
                    gh.maxPuppetErr, kRideTolCm);
            if (gh.maxPuppetErr > kRideTolCm) { Fail(g, "host", "the client's puppet strayed " + Num(gh.maxPuppetErr) + " cm from its seat during the drive"); return; }
            DC::Say("atvpass driven " + Num(moved));
            Next(g);
        } else if (Timed(g, kShortMs * 3)) {
            Throttle(gh.atv, false);
            Fail(g, "host", "the ATV moved only " + Num(moved) + " cm of " + Num(kDriveCm));
        }
        return;
    }

    case H::DriverLeaves: {
        if (!Seen("atvpass client-rode")) { if (Timed(g, kBoundMs)) Fail(g, "host", "the client never reported its ride"); return; }
        if (!HostLiveAtv()) { Fail(g, "host", "the ATV is gone"); return; }
        if (!g.flag) {
            g.flag = true;
            if (!A::Dismount(gh.atv)) { Fail(g, "host", "the ATV's own dismount did not run"); return; }
            SetBrake(gh.atv, true);   // a rig left rolling would carry the next boarding out of reach
            return;
        }
        const coop::atv_sync::TableView t = TableOf(gh.key);
        if (t.occupantSlot != 0xFF || coop::atv_sync::ActorSeatedBy(0) == gh.atv) {
            if (Timed(g, kShortMs)) Fail(g, "host", "the driver's seat did not clear on the host's table");
            return;
        }
        // The look of the seat, for a person: the host's own eyes, put a few metres from the rig (first its side, then its rear),
        // aimed at the rider.
        if (!gh.lookDone) {
            if (gh.lookStartMs == 0) gh.lookStartMs = Now();
            if (Now() - gh.lookStartMs < 1200) return;
            FVector root{}, seatAt{};
            FRotator rot{};
            if (!me || !A::GetRootTransform(gh.atv, root, rot) || !AP::SeatWorldLocation(gh.atv, 0, seatAt)) { gh.lookDone = true; return; }
            const FVector f = E::GetActorForwardVector(gh.atv), u = E::GetActorUpVector(gh.atv);
            const FVector r{u.Y * f.Z - u.Z * f.Y, u.Z * f.X - u.X * f.Z, u.X * f.Y - u.Y * f.X};
            if (gh.lookAimedMs == 0) {
                // The side view stands 3.3 m off the left; the rear view 3.3 m behind and a little right.
                const bool rear = gh.lookShot == 1;
                const FVector at{root.X + (rear ? -f.X * 330.f + r.X * 60.f : -r.X * 330.f), root.Y + (rear ? -f.Y * 330.f + r.Y * 60.f : -r.Y * 330.f),
                                 root.Z + 40.f};
                E::TeleportTo(me, at, FRotator{0.f, rot.Yaw, 0.f});
                gh.lookAimedMs = Now();
                gh.lookAimed = false;
                return;
            }
            if (!gh.lookAimed && Now() - gh.lookAimedMs >= 900) {
                E::SetControlRotation(E::GetController(me), coop::director::LookAt(E::GetCameraLocation(), seatAt));
                gh.lookAimed = true;
                gh.lookAimedMs = Now();
                return;
            }
            if (!gh.lookAimed || Now() - gh.lookAimedMs < 700) return;
            harness::screenshot::Capture(gh.lookShot == 0 ? L"atvpass-seat-side" : L"atvpass-seat-rear");
            if (++gh.lookShot >= 2) gh.lookDone = true;
            gh.lookAimedMs = 0;
            return;
        }
        bool attached = false;
        if (coop::atv_passenger::SeatOfSlot(gh.key, 1, &attached) != 0 || !attached) {
            Fail(g, "host", "the driver leaving ejected the passenger from the host's table or its puppet");
            return;
        }
        UE_LOGI("%s host: the driver left; the client still holds seat 0 and its puppet is still on the rig", kTag);
        DC::Say("atvpass driver-left");
        Next(g);
        return;
    }

    case H::WaitStayed:
        if (Seen("atvpass client-stayed")) Next(g);
        else if (Timed(g, kBoundMs)) Fail(g, "host", "the client never confirmed it stayed seated");
        return;

    case H::WaitLeft:
        if (Seen("atvpass client-left ")) Next(g);
        else if (Timed(g, kBoundMs)) Fail(g, "host", "the client never left its seat");
        return;

    case H::Judge2: {
        bool attached = true;
        const int seat = coop::atv_passenger::SeatOfSlot(gh.key, 1, &attached);
        coop::RemotePlayer* rp = coop::players::Registry::Get().Puppet(1);
        if (seat != -1 || attached || (rp && rp->valid() && rp->IsSeated())) {
            if (Timed(g, kShortMs)) Fail(g, "host", "the client's leave did not clear its seat on the host: seat " + std::to_string(seat) + ", puppet attached " +
                                                          std::to_string(attached ? 1 : 0));
            return;
        }
        UE_LOGI("%s host: the client's seat is free and its puppet follows the pose stream again", kTag);
        DC::Say("atvpass host-sees-left");
        Next(g);
        return;
    }

    // The host rides: the client drives the braked rig and the host's own player boards by the use key, judged by the same table.
    case H::ClientDrives:
        DC::Say("atvpass drive-by-client go");
        Next(g);
        return;
    case H::WaitClientSat:
        if (Seen("atvpass client-sat") && TableOf(gh.key).occupantSlot == 1) { gh.presses = 0; gh.pressAt = 0; Next(g); }
        else if (Timed(g, kBoundMs)) Fail(g, "host", "the client never sat in the ATV as the driver");
        return;
    case H::HostBoard: {
        if (!HostLiveAtv()) { Fail(g, "host", "the ATV is gone"); return; }
        if (coop::atv_passenger::ReadLocal().seated) {
            bool attached = false;
            const int seat = coop::atv_passenger::SeatOfSlot(gh.key, 0, &attached);
            if (seat != 0 || !attached || TableOf(gh.key).passengerSlot[0] != 0) {
                if (Timed(g, kShortMs * 3)) Fail(g, "host", "the host's board: table seat " + std::to_string(seat) + ", body attached " + std::to_string(attached ? 1 : 0));
                return;
            }
            UE_LOGI("%s host: boarded passenger seat 0 of the client's ATV through the use key (the host's own table)", kTag);
            DC::Say("atvpass host-boarded");
            Next(g);
            return;
        }
        if (Now() - gh.pressAt > 3000) {
            if (gh.presses >= 3) { Fail(g, "host", "three use presses at the client's ATV did not board the host"); return; }
            ++gh.presses;
            gh.pressAt = Now();
            FVector root{};
            AtvOf(gh.key, &gh.atv, &root);
            E::SetControlRotation(E::GetController(me), coop::director::LookAt(E::GetCameraLocation(), root));
            if (!PressUse(me, gh.atv)) Fail(g, "host", "the use input event did not dispatch");
        }
        return;
    }
    case H::HostRideWait:
        if (Seen("atvpass client-sees-host-rider")) Next(g);
        else if (Timed(g, kBoundMs)) Fail(g, "host", "the client never saw the host's body on its copy of the rig");
        return;
    case H::HostLeave: {
        if (!g.flag) {
            if (Now() - g.stepAt < 1000) return;
            g.flag = true;
            gh.leave = LeaveCheck{};
            if (!PressUse(me, nullptr)) Fail(g, "host", "the use input event did not dispatch for the leave");
            return;
        }
        if (coop::atv_passenger::LocalIsPassenger()) { if (Timed(g, kShortMs)) Fail(g, "host", "the use key did not leave the seat"); return; }
        const int r = CheckLeft(gh.leave, me, gh.atv);
        if (r < 0) { Fail(g, "host", gh.leave.why); return; }
        if (r == 0) return;
        UE_LOGI("%s host: left its passenger seat through the use key and stood %.0f cm from the ATV, %.0f cm above its root", kTag, gh.leave.horiz, gh.leave.up);
        DC::Say("atvpass host-left");
        Next(g);
        return;
    }
    case H::ClientUndrove:
        if (Seen("atvpass client-undrove") && TableOf(gh.key).occupantSlot == 0xFF) Next(g);
        else if (Timed(g, kBoundMs)) Fail(g, "host", "the client never got out of the driver's seat");
        return;

    // The red arms, each a raw request the client sends and the host's counters judge.
    case H::RedDupGo:
        if (Now() - g.stepAt < kBucketGapMs) return;
        gh.base = AU::ReadCounters();
        DC::Say("atvpass red-dup go");
        Next(g);
        return;
    case H::RedDupWait: {
        const AU::Counters c = AU::ReadCounters();
        const unsigned idx = static_cast<unsigned>(AtvPassengerRefusal::AlreadySeated);
        if (c.passengerRefused[idx] > gh.base.passengerRefused[idx]) {
            UE_LOGI("%s host: a duplicate board from a rider was refused already_seated (%llu)", kTag, c.passengerRefused[idx]);
            Next(g);
        } else if (Timed(g, kShortMs)) {
            Fail(g, "host", "the duplicate board while seated was not refused already_seated");
        }
        return;
    }

    case H::RedLeave:
        DC::Say("atvpass red-leave go");
        Next(g);
        return;
    case H::RedLeaveWait: {
        bool attached = true;
        if (Seen("atvpass client-left2") && coop::atv_passenger::SeatOfSlot(gh.key, 1, &attached) == -1) Next(g);
        else if (Timed(g, kBoundMs)) Fail(g, "host", "the client never left the second time");
        return;
    }

    case H::FullArm:
        if (!HostLiveAtv()) { Fail(g, "host", "the ATV is gone"); return; }
        if (Now() - g.stepAt < kBucketGapMs) return;
        AU::DrillSetPassengerGhosts(gh.key, true);
        gh.base = AU::ReadCounters();
        DC::Say("atvpass red-full go");
        Next(g);
        return;
    case H::FullWait: {
        const AU::Counters c = AU::ReadCounters();
        const unsigned idx = static_cast<unsigned>(AtvPassengerRefusal::Full);
        if (c.passengerRefused[idx] > gh.base.passengerRefused[idx]) {
            AU::DrillSetPassengerGhosts(gh.key, false);
            UE_LOGI("%s host: a board with every seat taken was refused full (%llu)", kTag, c.passengerRefused[idx]);
            Next(g);
        } else if (Timed(g, kShortMs)) {
            AU::DrillSetPassengerGhosts(gh.key, false);
            Fail(g, "host", "the board with every seat taken was not refused full");
        }
        return;
    }

    case H::ReachArm: {
        if (!Seen("atvpass raw full") || Now() - g.stepAt < kBucketGapMs) { if (Timed(g, kBoundMs)) Fail(g, "host", "the client never sent its second raw request"); return; }
        if (!HostLiveAtv()) { Fail(g, "host", "the ATV is gone"); return; }
        FVector root{};
        FRotator rot{};
        if (!A::GetRootTransform(gh.atv, root, rot) || !A::TeleportRig(gh.atv, {root.X + kAwayCm, root.Y, root.Z + 40.f}, rot)) {
            Fail(g, "host", "the host could not move its ATV away");
            return;
        }
        gh.base = AU::ReadCounters();
        DC::Say("atvpass red-reach go");
        Next(g);
        return;
    }
    case H::ReachWait: {
        const AU::Counters c = AU::ReadCounters();
        const unsigned idx = static_cast<unsigned>(AtvPassengerRefusal::OutOfReach);
        if (c.passengerRefused[idx] > gh.base.passengerRefused[idx]) {
            UE_LOGI("%s host: a board out of reach was refused out_of_reach (%llu)", kTag, c.passengerRefused[idx]);
            Next(g);
        } else if (Timed(g, kShortMs)) {
            Fail(g, "host", "the board out of reach was not refused out_of_reach");
        }
        return;
    }

    case H::Finish: {
        const AU::Counters c = AU::ReadCounters();
        DC::Say("atvpass done");
        char nums[200];
        std::snprintf(nums, sizeof(nums), "admitted=%llu refused: already_seated=%llu full=%llu out_of_reach=%llu paced=%llu",
                      c.passengerAdmitted, c.passengerRefused[static_cast<unsigned>(AtvPassengerRefusal::AlreadySeated)],
                      c.passengerRefused[static_cast<unsigned>(AtvPassengerRefusal::Full)],
                      c.passengerRefused[static_cast<unsigned>(AtvPassengerRefusal::OutOfReach)],
                      c.passengerRefused[static_cast<unsigned>(AtvPassengerRefusal::Paced)]);
        Pass(g, "host", std::string("the client boarded seat 0 and rode the drive within tolerance (puppet max ") + Num(gh.maxPuppetErr) +
                            " cm), stayed when the driver left, left beside the ATV; the host rode the client's ATV the same way; the client boarded again and an Unstuck teleport took it off the seat, and the host refused its duplicate, full and out-of-reach boards -- " +
                            nums);
        return;
    }
    }
}

// ---- the client -------------------------------------------------------------------------------------------------

enum class C : int {
    Ready, WaitKey, WaitAtv, Walk, WaitHostSat, Board, Armed, Ride, Stay, Leave, WaitDriveCue, SitDriver, WaitHostBoard, WaitHostLeft, WaitSit2, Walk2,
    Board2, DupGo, DupSent,
    Leave2Go, Leave2, FullGo, FullSent, ReachGo, ReachSent, Finish
};

struct Client {
    Shared                                          g;
    std::wstring                                    key;
    void*                                           atv = nullptr;
    std::shared_ptr<coop::director::BackgroundWalk> walk;
    FVector                                         boardedAt{};
    float                                           maxErr = 0.f;
    float                                           moved = 0.f;
    int                                             presses = 0;
    uint64_t                                        pressAt = 0;
    bool                                            readySaid = false;
    coop::director::ClearHand                       clear;
    bool                                            cleared = false;
    atv_seat::SitTry                                sit;
    LeaveCheck                                      leave;
};
Client gc;

void WalkCancel() {
    if (gc.walk && gc.walk->state.load() == 0) coop::director::CancelBackgroundWalk(*gc.walk);
    gc.walk.reset();
}

bool StreamsRun() { return coop::net_pump::HasAnnouncedWorldReady() && coop::players::Registry::Get().Local() != nullptr; }

bool SendRaw(coop::net::Session& s, bool board, uint8_t seq) {
    coop::net::AtvPassengerRequestPayload p{};
    coop::net::WireKeyFromString(gc.key, p.key);
    p.action = board ? 1 : 0;
    p.seq = seq;
    return s.SendReliable(coop::net::ReliableKind::AtvPassengerRequest, &p, sizeof(p));
}

// Is this machine's body a passenger of the ATV, on the seat the table names, attached?
bool Seated(int* seatOut = nullptr) {
    const coop::atv_passenger::LocalView v = coop::atv_passenger::ReadLocal();
    if (!v.seated || v.key != gc.key) return false;
    bool attached = false;
    const int seat = coop::atv_passenger::SeatOfSlot(gc.key, coop::players::Registry::Get().LocalPeerId(), &attached);
    if (seatOut) *seatOut = seat;
    return attached && seat == v.seat && seat >= 0;
}

void ClientTick(coop::net::Session* s) {
    Shared& g = gc.g;
    if (Seen("atvpass host FAIL")) { WalkCancel(); Fail(g, "client", "the host reported a failed arm (its own line has why)"); return; }
    void* player = coop::players::Registry::Get().Local();
    if (!player || !StreamsRun()) return;
    if (g.stepAt == 0) g.stepAt = Now();
    switch (static_cast<C>(g.step)) {
    case C::Ready:
        if (!gc.readySaid) { gc.readySaid = true; DC::Say("atvpass client-ready"); }
        if (Seen("atvpass host-ready")) Next(g);
        return;

    case C::WaitKey: {
        std::string rest;
        if (DC::SeenRestOf("atvpass key ", &rest) && Seen("atvpass parked")) {
            gc.key.assign(rest.begin(), rest.end());
            UE_LOGI("%s client: the host's ATV is '%ls'", kTag, gc.key.c_str());
            Next(g);
        } else if (Timed(g, kBoundMs)) {
            Fail(g, "client", "the host never named its ATV");
        }
        return;
    }

    case C::WaitAtv: {
        FVector root{};
        if (AtvOf(gc.key, &gc.atv, &root)) {
            gc.walk = coop::director::StartBackgroundWalk(root, kWalkReachCm, kWalkDeadlineS);
            UE_LOGI("%s client: walking to the ATV at (%.0f,%.0f,%.0f)", kTag, root.X, root.Y, root.Z);
            Next(g);
        } else if (Timed(g, kBoundMs)) {
            Fail(g, "client", "the host's ATV never entered this table");
        }
        return;
    }

    case C::Walk:
    case C::Walk2: {
        const int st = gc.walk ? gc.walk->state.load() : 2;
        if (st == 2) { Fail(g, "client", "the director's walk to the ATV did not arrive"); return; }
        if (st == 1) { gc.walk.reset(); Next(g); }
        return;
    }

    // The host rides: this client drives the braked rig, and the host boards a passenger seat of it.
    case C::WaitDriveCue:
        if (Seen("atvpass drive-by-client go")) { gc.sit = atv_seat::SitTry{}; Next(g); }
        else if (Timed(g, kBoundMs)) Fail(g, "client", "the host never cued the drive by the client");
        return;
    case C::SitDriver:
        if (!AtvOf(gc.key, &gc.atv)) { Fail(g, "client", "the ATV is gone"); return; }
        if (!atv_seat::TrySit(player, gc.atv, gc.sit)) { if (Timed(g, kShortMs * 3)) Fail(g, "client", "the client's sit as the driver never finished"); return; }
        if (coop::atv_sync::ActorSeatedBy(coop::players::Registry::Get().LocalPeerId()) != gc.atv) { Fail(g, "client", "the client's sit as the driver did not take"); return; }
        UE_LOGI("%s client: sat as the driver", kTag);
        DC::Say("atvpass client-sat");
        Next(g);
        return;
    case C::WaitHostBoard: {
        bool attached = false;
        if (Seen("atvpass host-boarded")) {
            const int seat = coop::atv_passenger::SeatOfSlot(gc.key, 0, &attached);
            coop::RemotePlayer* rp = coop::players::Registry::Get().Puppet(0);
            if (seat == 0 && attached && rp && rp->valid() && rp->IsSeated()) {
                UE_LOGI("%s client: the host's puppet sits at seat 0 on this machine's copy of the rig", kTag);
                DC::Say("atvpass client-sees-host-rider");
                Next(g);
                return;
            }
            if (Timed(g, kShortMs * 3)) Fail(g, "client", "the host's board: table seat " + std::to_string(seat) + ", its puppet attached " + std::to_string(attached ? 1 : 0));
        } else if (Timed(g, kBoundMs)) {
            Fail(g, "client", "the host never boarded");
        }
        return;
    }
    case C::WaitHostLeft: {
        if (!Seen("atvpass host-left")) { if (Timed(g, kBoundMs)) Fail(g, "client", "the host never left its seat"); return; }
        if (!g.flag) {
            bool attached = true;
            if (coop::atv_passenger::SeatOfSlot(gc.key, 0, &attached) != -1 || attached) { if (Timed(g, kShortMs)) Fail(g, "client", "the host's leave did not clear its seat on this machine's table or puppet"); return; }
            UE_LOGI("%s client: the host's seat is free and its puppet follows the pose stream again; getting out of the driver's seat", kTag);
            g.flag = true;
            if (!A::Dismount(gc.atv)) Fail(g, "client", "the ATV's own dismount did not run");
            return;
        }
        if (coop::atv_sync::ActorSeatedBy(coop::players::Registry::Get().LocalPeerId()) == gc.atv) { if (Timed(g, kShortMs)) Fail(g, "client", "the client is still the driver"); return; }
        DC::Say("atvpass client-undrove");
        Next(g);
        return;
    }

    case C::WaitHostSat:
    case C::WaitSit2:
        if (!AtvOf(gc.key, &gc.atv)) { Fail(g, "client", "the ATV is gone"); return; }
        if (Seen(static_cast<C>(g.step) == C::WaitHostSat ? "atvpass host-sat" : "atvpass host-sat2") &&
            TableOf(gc.key).occupantSlot == 0) {
            gc.presses = 0;
            gc.pressAt = 0;
            if (static_cast<C>(g.step) == C::WaitSit2) {
                // The rig may have rolled after the first ride: the second boarding starts from a walk to where it is now.
                FVector root{};
                AtvOf(gc.key, &gc.atv, &root);
                gc.walk = coop::director::StartBackgroundWalk(root, kWalkReachCm, kWalkDeadlineS);
            }
            Next(g);
        } else if (Timed(g, kBoundMs)) {
            Fail(g, "client", "the host never sat in the ATV (its seat never reached this table)");
        }
        return;

    case C::Board:
    case C::Board2: {
        const bool second = static_cast<C>(g.step) == C::Board2;
        if (!AtvOf(gc.key, &gc.atv)) { Fail(g, "client", "the ATV is gone"); return; }
        int seat = -1;
        if (Seated(&seat)) {
            // The use key took the seat on this machine and the table (the answer or the stamp) agrees.
            const coop::atv_sync::TableView t = TableOf(gc.key);
            if (t.passengerSlot[seat] != coop::players::Registry::Get().LocalPeerId()) return;
            if (second ? seat > 1 : seat != 0) { Fail(g, "client", "the first board took seat " + std::to_string(seat) + ", not 0"); return; }
            E::TryGetActorLocation(player, gc.boardedAt);
            UE_LOGI("%s client: boarded seat %d through the use key (the host's table agrees)", kTag, seat);
            DC::Say(second ? "atvpass client-boarded2" : "atvpass client-boarded");
            Next(g);
            return;
        }
        if (!gc.cleared) {
            const coop::director::ClearState cs = gc.clear.Tick();
            if (cs == coop::director::ClearState::Failed) { Fail(g, "client", std::string("the client's hand could not be cleared: ") + gc.clear.Why()); return; }
            if (cs == coop::director::ClearState::Working) return;
            gc.cleared = true;
        }
        if (Now() - gc.pressAt > 3000) {
            if (gc.presses >= 3) { Fail(g, "client", "three use presses at the ATV did not board a passenger seat"); return; }
            ++gc.presses;
            gc.pressAt = Now();
            FVector root{};
            AtvOf(gc.key, &gc.atv, &root);
            E::SetControlRotation(E::GetController(player), coop::director::LookAt(E::GetCameraLocation(), root));
            UE_LOGI("%s client: the use key at the ATV (press %d)", kTag, gc.presses);
            if (!PressUse(player, gc.atv)) Fail(g, "client", "the use input event did not dispatch");
        }
        return;
    }

    case C::Armed:
        gc.maxErr = 0.f;
        DC::Say("atvpass client-armed");
        Next(g);
        return;

    case C::Ride: {
        FVector seatAt{}, at{};
        int seat = -1;
        if (!Seated(&seat)) { Fail(g, "client", "the rider lost its seat during the ride"); return; }
        if (AP::SeatWorldLocation(gc.atv, seat, seatAt) && E::TryGetActorLocation(player, at)) {
            const float err = Dist(at, seatAt);
            if (err > gc.maxErr) gc.maxErr = err;
            gc.moved = Dist(at, gc.boardedAt);
        }
        std::string rest;
        if (Seen("atvpass driven") && DC::SeenRestOf("atvpass driven ", &rest)) {
            const float driven = static_cast<float>(std::atof(rest.c_str()));
            UE_LOGI("%s client: the host drove %.0f cm; this body moved %.0f cm and stayed within %.0f cm of its seat (tolerance %.0f)",
                    kTag, driven, gc.moved, gc.maxErr, kRideTolCm);
            if (gc.maxErr > kRideTolCm) { Fail(g, "client", "the body strayed " + Num(gc.maxErr) + " cm from its seat during the ride"); return; }
            if (gc.moved < driven * kFollowFrac) { Fail(g, "client", "the body moved " + Num(gc.moved) + " cm of the " + Num(driven) + " the rig was driven"); return; }
            DC::Say("atvpass client-rode " + Num(gc.maxErr));
            Next(g);
        } else if (Timed(g, kBoundMs)) {
            Fail(g, "client", "the host never finished its drive");
        }
        return;
    }

    case C::Stay:
        if (!Seen("atvpass driver-left")) { if (Timed(g, kBoundMs)) Fail(g, "client", "the driver never left"); return; }
        if (!g.flag) { g.flag = true; g.flagAt = Now(); return; }
        if (Now() - g.flagAt < 2500) {
            if (!Seated()) Fail(g, "client", "the driver leaving ejected this passenger");
            return;
        }
        if (TableOf(gc.key).passengerSlot[0] != coop::players::Registry::Get().LocalPeerId()) { Fail(g, "client", "the table no longer seats this passenger"); return; }
        UE_LOGI("%s client: still seated two seconds after the driver left", kTag);
        DC::Say("atvpass client-stayed");
        Next(g);
        return;

    case C::Leave:
    case C::Leave2: {
        const bool second = static_cast<C>(g.step) == C::Leave2;
        if (!g.flag) {
            g.flag = true;
            g.flagAt = Now();
            gc.leave = LeaveCheck{};
            if (second) {
                // The second leave is a teleport of the body: the F1 panel's Unstuck, which every cheat teleport shares with it.
                UE_LOGI("%s client: Unstuck while seated", kTag);
                coop::cheats::self_teleport::Unstuck();
            } else if (!PressUse(player, nullptr)) {
                Fail(g, "client", "the use input event did not dispatch for the leave");
            }
            return;
        }
        if (Seated() || coop::atv_passenger::LocalIsPassenger()) { if (Timed(g, kShortMs)) Fail(g, "client", second ? "the Unstuck teleport did not take the body off its seat" : "the use key did not leave the seat"); return; }
        if (second) {
            // Off the seat on this machine, and the host's table agrees (the host's arm judges its side); the body lifted and landed on a floor.
            if (Now() - g.flagAt < 2500) return;
            FVector root{}, at{};
            FRotator rot{};
            E::FloorRead fr;
            const bool floor = E::ReadCurrentFloor(player, &fr) && fr.blocking && fr.walkable;
            if (!A::GetRootTransform(gc.atv, root, rot) || !E::TryGetActorLocation(player, at)) { Fail(g, "client", "the ATV or the body does not read after the Unstuck"); return; }
            if (AP::IsRiderOf(player, gc.atv) || !floor || HorizDist(at, root) > 500.f) {
                Fail(g, "client", "after the Unstuck the body is attached " + std::to_string(AP::IsRiderOf(player, gc.atv) ? 1 : 0) + ", on a floor " + std::to_string(floor ? 1 : 0) + ", " + Num(HorizDist(at, root)) + " cm from the ATV");
                return;
            }
            UE_LOGI("%s client: the Unstuck teleport left the seat and the body stands on a floor %.0f cm from the ATV", kTag, HorizDist(at, root));
            DC::Say("atvpass client-left2");
            Next(g);
            return;
        }
        const int r = CheckLeft(gc.leave, player, gc.atv);
        if (r < 0) { Fail(g, "client", gc.leave.why); return; }
        if (r == 0) return;
        UE_LOGI("%s client: left through the use key and stood %.0f cm from the ATV, %.0f cm above its root, on a walkable floor (floor at leave %d)", kTag,
                gc.leave.horiz, gc.leave.up, gc.leave.floorAtLeave ? 1 : 0);
        DC::Say(second ? "atvpass client-left2" : "atvpass client-left " + Num(gc.leave.horiz));
        Next(g);
        return;
    }

    case C::DupGo:
        if (Seen("atvpass red-dup go")) {
            if (!Seated() || !SendRaw(*s, true, kRawSeqBase)) { Fail(g, "client", "the duplicate board could not be sent from a seated body"); return; }
            UE_LOGI("%s client: sent a duplicate board while seated", kTag);
            DC::Say("atvpass raw dup");
            Next(g);
        } else if (Timed(g, kBoundMs)) {
            Fail(g, "client", "the host never cued the duplicate board");
        }
        return;
    case C::DupSent:
        if (Seen("atvpass red-leave go")) Next(g);
        else if (Timed(g, kBoundMs)) Fail(g, "client", "the host never judged the duplicate board");
        return;
    case C::Leave2Go:
        g.flag = false;
        Next(g);
        return;
    case C::FullGo:
        if (Seen("atvpass red-full go")) {
            if (Seated() || !SendRaw(*s, true, kRawSeqBase + 1)) { Fail(g, "client", "the board for the full arm could not be sent"); return; }
            UE_LOGI("%s client: sent a board with every seat taken", kTag);
            DC::Say("atvpass raw full");
            Next(g);
        } else if (Timed(g, kBoundMs)) {
            Fail(g, "client", "the host never cued the full arm");
        }
        return;
    case C::FullSent:
        if (Seen("atvpass red-reach go")) Next(g);
        else if (Timed(g, kBoundMs)) Fail(g, "client", "the host never moved its ATV");
        return;
    case C::ReachGo:
        if (Seated() || !SendRaw(*s, true, kRawSeqBase + 2)) { Fail(g, "client", "the board for the reach arm could not be sent"); return; }
        UE_LOGI("%s client: sent a board while the ATV is %.0f cm away", kTag, kAwayCm);
        DC::Say("atvpass raw reach");
        Next(g);
        return;
    case C::ReachSent:
        if (Seen("atvpass done")) Next(g);
        else if (Timed(g, kBoundMs)) Fail(g, "client", "the host never finished");
        return;
    case C::Finish:
        Pass(g, "client", "boarded through the use key twice, rode within tolerance, stayed when the driver left, left beside the ATV, saw the host ride its rig and left a seat by a teleport");
        return;
    }
}

}  // namespace

void Tick(coop::net::Session* session) {
    if (!Enabled() || !session || !session->running()) return;
    if (session->role() == coop::net::Role::Host) {
        if (!gh.g.done) HostTick(session);
    } else if (!gc.g.done) {
        ClientTick(session);
    }
}

void OnDisconnect() {
    WalkCancel();
    gh = Host{};
    gc = Client{};
}

}  // namespace coop::dev::atv_passenger_drill
