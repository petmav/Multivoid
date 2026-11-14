// coop/dev/suitcase_probe.cpp -- see coop/dev/suitcase_probe.h.

#include "coop/dev/suitcase_probe.h"

#include "coop/cheats/item_spawn.h"
#include "coop/config/config.h"
#include "coop/net/net_clock.h"
#include "coop/player/players_registry.h"
#include "coop/player/roster.h"

#include "ue_wrap/actors/container_inventory.h"
#include "ue_wrap/core/cached_obj_ref.h"
#include "ue_wrap/core/field_io.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/object_index.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/engine/engine.h"

#include <cstdint>
#include <string>
#include <vector>

namespace coop::dev::suitcase_probe {
namespace {

namespace R  = ue_wrap::reflection;
namespace E  = ue_wrap::engine;
namespace CI = ue_wrap::container_inventory;

constexpr uint64_t kSettleMs = 20000;
constexpr uint64_t kFillMs = 5000;
constexpr uint64_t kClientReadMs = 60000;

bool On() {
    static const bool on = coop::config::ResolveFlag(::coop::config_registry::rows::suitcase_probe);
    return on;
}

ue_wrap::CachedObjRef g_spawned;
uint64_t g_worldMs = 0;
uint64_t g_spawnedMs = 0;
bool     g_done = false;

// The names in a TArray<FName> member of `obj`, joined with commas.
std::wstring NameArray(void* obj, const wchar_t* member) {
    const int32_t off = R::FindPropertyOffset(R::ClassOf(obj), member);
    if (off < 0) return L"<no member>";
    const auto* arr = reinterpret_cast<const ue_wrap::field_io::TArrayView*>(reinterpret_cast<const uint8_t*>(obj) + off);
    std::wstring out;
    for (int32_t i = 0; arr->data && i < arr->num && i < 64; ++i) {
        if (i) out += L",";
        out += R::ToString(*reinterpret_cast<const R::FName*>(arr->data + static_cast<size_t>(i) * sizeof(R::FName)));
    }
    return out.empty() ? L"<empty>" : out;
}

std::wstring Name(void* obj, const wchar_t* member) {
    const int32_t off = R::FindPropertyOffset(R::ClassOf(obj), member);
    if (off < 0) return L"<no member>";
    return R::ToString(*reinterpret_cast<const R::FName*>(reinterpret_cast<const uint8_t*>(obj) + off));
}

void Describe(const char* what, void* suitcase) {
    std::vector<CI::Item> items;
    int32_t count = -1;
    void* inv = CI::InventoryOf(suitcase);
    const bool read = inv && CI::ReadContents(inv, items, 64, &count);
    std::wstring names;
    for (const CI::Item& it : items) {
        if (!names.empty()) names += L",";
        names += it.name;
    }
    UE_LOGI("[SUITCASE-PROBE] %s: starterLoot=[%ls] spawnLoot=%ls contents(read=%d count=%d)=[%ls]", what,
            NameArray(suitcase, L"starterLootAsDefaultItems").c_str(), Name(suitcase, L"spawnLoot").c_str(), read ? 1 : 0,
            count, names.c_str());
}

}  // namespace

void DescribeWorld() {
    if (!On()) return;
    void* cls = ue_wrap::object_index::ClassByName(L"prop_container_suitcase_C");
    int seen = 0;
    if (cls) {
        ue_wrap::object_index::ForEachInstance(cls, [](void* ctx, void* obj, int32_t) {
            if (!obj || !R::IsLive(obj) || R::NameStartsWith(R::NameOf(obj), L"Default__")) return;
            ++*static_cast<int*>(ctx);
            Describe("a suitcase of this world", obj);
        }, &seen);
    } else if (void* one = R::FindObjectByClass(L"prop_container_suitcase_C")) {
        ++seen;
        Describe("the first suitcase found", one);
    }
    UE_LOGI("[SUITCASE-PROBE] DONE (%d suitcase(s) described, class %s)", seen, cls ? "indexed" : "not indexed");
}

void Tick() {
    if (g_done || !On()) return;
    const uint64_t now = coop::net::NowMs();
    void* me = coop::players::Registry::Get().Local();
    if (!me) return;
    if (!g_worldMs) g_worldMs = now;
    // A client only reads: a minute into its world it prints the suitcases it holds, the host's gifts among them.
    if (!coop::roster::LocalIsHost()) {
        if (now - g_worldMs < kClientReadMs) return;
        DescribeWorld();
        g_done = true;
        return;
    }
    if (now - g_worldMs < kSettleMs) return;
    void* cls = ue_wrap::object_index::ClassByName(L"prop_container_suitcase_C");
    if (!cls) cls = R::FindClass(L"prop_container_suitcase_C");
    if (!g_spawnedMs) {
        if (void* cdo = R::FindClassDefaultObject(L"prop_container_suitcase_C")) Describe("class defaults", cdo);
        else UE_LOGI("[SUITCASE-PROBE] the suitcase class has no default object in this process (class %s)", cls ? "found" : "absent");
        if (cls) {
            struct Ctx { void* skip; } ctx{nullptr};
            ue_wrap::object_index::ForEachInstance(cls, [](void*, void* obj, int32_t) {
                if (obj && R::IsLive(obj) && !R::NameStartsWith(R::NameOf(obj), L"Default__")) Describe("a suitcase of this world", obj);
            }, &ctx);
        }
        ue_wrap::FVector at{};
        if (!cls || !E::TryGetActorLocation(me, at)) {
            UE_LOGW("[SUITCASE-PROBE] DONE no class or no player place -- nothing spawned");
            g_done = true;
            return;
        }
        const ue_wrap::FVector fwd = E::GetActorForwardVector(me);
        void* spawned = E::SpawnActor(cls, {at.X + fwd.X * 150.f, at.Y + fwd.Y * 150.f, at.Z + 30.f});
        g_spawned.Set(spawned);
        g_spawnedMs = now;
        UE_LOGI("[SUITCASE-PROBE] spawned a suitcase in front of this player: %d", spawned ? 1 : 0);
        // The cheat spawner on three list names with no class of their own, and one with.
        for (const char* name : {"mre", "tv", "batts", "crowbar"}) {
            int made = 0;
            const auto r = coop::cheats::item_spawn::Spawn(name, 1, 0, true, &made);
            UE_LOGI("[SUITCASE-PROBE] item spawner '%s': result %d, %d made", name, static_cast<int>(r), made);
        }
        if (!spawned) g_done = true;
        return;
    }
    if (now - g_spawnedMs < kFillMs) return;
    if (void* s = g_spawned.Get()) Describe("the spawned suitcase, five seconds on", s);
    UE_LOGI("[SUITCASE-PROBE] DONE");
    g_done = true;
}

}  // namespace coop::dev::suitcase_probe
