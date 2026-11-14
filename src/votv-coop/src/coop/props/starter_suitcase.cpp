// coop/props/starter_suitcase.cpp -- see coop/props/starter_suitcase.h.

#include "coop/props/starter_suitcase.h"

#include "coop/config/config.h"
#include "coop/net/net_clock.h"
#include "coop/net/session.h"
#include "coop/player/players_registry.h"
#include "coop/player/remote_player.h"
#include "coop/props/prop_snapshot.h"
#include "coop/save/join_window_baseline.h"

#include "ue_wrap/actors/container_inventory.h"
#include "ue_wrap/core/asset_load.h"
#include "ue_wrap/core/call.h"
#include "ue_wrap/core/fname_utils.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/object_index.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/trace.h"
#include "ue_wrap/core/types.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/world/world_singleton.h"

#include <cmath>

namespace coop::starter_suitcase {
namespace {

namespace R  = ue_wrap::reflection;
namespace E  = ue_wrap::engine;
namespace CI = ue_wrap::container_inventory;

// What a new game's suitcase gathers, by the game's item list names, as a fresh level shows it.
constexpr const wchar_t* kStarterItems[] = {L"signframe_0", L"mre", L"mre", L"batts", L"batts", L"kelplush", L"toiletroll"};

constexpr uint64_t kStepMs = 1000;
constexpr uint64_t kSettleMs = 4000;   // the joiner's world has stood this long before the suitcase comes
constexpr float kBesideCm = 110.f;
constexpr float kLiftCm = 40.f;

bool g_owed[coop::players::kMaxPeers] = {};
uint64_t g_readySinceMs[coop::players::kMaxPeers] = {};
uint64_t g_nextMs = 0;
uint64_t g_given = 0;
uint64_t g_items = 0;

bool On() {
    static const bool on = coop::config::ResolveFlag(::coop::config_registry::rows::net_starter_suitcase);
    return on;
}

void* SuitcaseClass() {
    if (void* cls = ue_wrap::object_index::ClassByName(L"prop_container_suitcase_C")) return cls;
    return ue_wrap::asset_load::LoadObjectByPath(L"/Game/objects/prop_container_suitcase.prop_container_suitcase_C");
}

// The gamemode's own spawn of one item of its list, at `at`. The actor it made, or null.
void* SpawnItem(void* gamemode, void* fn, const wchar_t* name, const ue_wrap::FVector& at) {
    ue_wrap::ParamFrame f(fn);
    if (!f.valid()) return nullptr;
    const R::FName row = ue_wrap::fname_utils::StringToFName(name);
    const ue_wrap::FTransform xform = ue_wrap::MakeTransform(at);
    if (!f.SetRaw(L"prop", &row, sizeof(row)) || !f.SetRaw(L"InputPin", &xform, sizeof(xform)) ||
        !f.Set<int32_t>(L"Amount", 1) || !ue_wrap::Call(gamemode, f))
        return nullptr;
    return f.Get<void*>(L"actor ");  // the out pin's name carries a trailing space in the game's graph
}

// Spawn the suitcase beside the player in `slot` and fill it. False when the place or a class is not there yet.
bool Give(int slot) {
    coop::RemotePlayer* const puppet = coop::players::Registry::Get().Puppet(static_cast<uint8_t>(slot));
    ue_wrap::FVector at{};
    if (!puppet || !puppet->valid() || !puppet->HasPose() || !puppet->TryGetLocation(at)) return false;
    void* gamemode = ue_wrap::world_singleton::Gamemode();
    if (!gamemode) return false;
    void* spawnFn = gamemode ? R::FindFunction(R::ClassOf(gamemode), L"spawnPropThroughGamemode") : nullptr;
    void* cls = SuitcaseClass();
    if (!spawnFn || !cls) return false;
    // Beside the player: to their right, left, front or back, the first of those a line from them reaches
    // unblocked, so a wall they stand against does not take the suitcase. With none clear it comes at their feet.
    const ue_wrap::FVector aim = puppet->GetSyncedAimDirection();
    const float len = std::sqrt(aim.X * aim.X + aim.Y * aim.Y);
    const float fx = len > 0.001f ? aim.X / len : 1.f;
    const float fy = len > 0.001f ? aim.Y / len : 0.f;
    const float sides[4][2] = {{-fy, fx}, {fy, -fx}, {fx, fy}, {-fx, -fy}};
    ue_wrap::FVector where{at.X, at.Y, at.Z + kLiftCm};
    for (const auto& side : sides) {
        const ue_wrap::FVector spot{at.X + side[0] * kBesideCm, at.Y + side[1] * kBesideCm, at.Z + kLiftCm};
        if (ue_wrap::trace::LineBlockedStatDyn(gamemode, {at.X, at.Y, at.Z + kLiftCm}, spot) != 0) continue;
        where = spot;
        break;
    }
    void* suitcase = E::SpawnActor(cls, where);
    void* inventory = suitcase ? CI::InventoryOf(suitcase) : nullptr;
    if (!inventory) {
        UE_LOGW("starter_suitcase: slot %d -- the suitcase did not spawn or has no inventory (%p)", slot, suitcase);
        return true;  // a spawn that fails in a ready world is not asked again
    }
    int filed = 0;
    for (const wchar_t* name : kStarterItems) {
        void* item = SpawnItem(gamemode, spawnFn, name, {where.X, where.Y, where.Z + 60.f});
        if (!item) {
            UE_LOGW("starter_suitcase: slot %d -- the item list made no '%ls'", slot, name);
            continue;
        }
        if (CI::AddItemAsObject(inventory, item)) ++filed;
        else UE_LOGW("starter_suitcase: slot %d -- the suitcase did not take '%ls'; it lies beside it", slot, name);
    }
    ++g_given;
    g_items += static_cast<uint64_t>(filed);
    UE_LOGI("starter_suitcase: slot %d's first join -- a suitcase beside them at (%.0f,%.0f,%.0f) with %d of %zu items",
            slot, where.X, where.Y, where.Z, filed, sizeof(kStarterItems) / sizeof(kStarterItems[0]));
    return true;
}

}  // namespace

void NoteFirstJoin(int slot) {
    if (!On() || slot < 1 || slot >= coop::players::kMaxPeers) return;
    g_owed[slot] = true;
    g_readySinceMs[slot] = 0;
}

void NoteReturning(int slot) {
    if (slot < 1 || slot >= coop::players::kMaxPeers) return;
    g_owed[slot] = false;
    g_readySinceMs[slot] = 0;
}

void Tick(coop::net::Session* session) {
    if (!session || !On()) return;
    const uint64_t now = coop::net::NowMs();
    if (now < g_nextMs) return;
    g_nextMs = now + kStepMs;
    for (int slot = 1; slot < coop::players::kMaxPeers; ++slot) {
        if (!g_owed[slot]) continue;
        const bool inWorld = session->IsSlotWorldReady(slot) && coop::prop_snapshot::IsBracketClosed(slot) &&
                             !coop::join_window_baseline::IsLateWindowOpen(slot);
        if (!inWorld) {
            g_readySinceMs[slot] = 0;
            continue;
        }
        if (!g_readySinceMs[slot]) g_readySinceMs[slot] = now;
        if (now - g_readySinceMs[slot] < kSettleMs) continue;
        if (Give(slot)) g_owed[slot] = false;
    }
}

uint64_t Given() { return g_given; }
uint64_t ItemsFiled() { return g_items; }

void OnDisconnect() {
    for (bool& owed : g_owed) owed = false;
    for (uint64_t& t : g_readySinceMs) t = 0;
    g_nextMs = 0;
}

}  // namespace coop::starter_suitcase
