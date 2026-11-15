// ue_wrap/actors/prop_events.cpp -- see ue_wrap/actors/prop_events.h.

#include "ue_wrap/actors/prop_events.h"

#include "ue_wrap/core/cached_obj_ref.h"
#include "ue_wrap/core/call.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/object_index.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/sdk_profile.h"

#include <cstdint>

namespace ue_wrap::prop {
namespace {

namespace P = profile;
namespace R = reflection;

// One of prop_C's own events, found on prop_C itself (FindFunction does not climb, which is the
// point: an override on a subclass is a different function). Resolved once the class has loaded;
// an event missing from a loaded class is said once and never looked up again.
struct BaseEvent {
    const wchar_t* name;
    ue_wrap::CachedObjRef cls;
    void* fn = nullptr;
    bool  missed = false;
};

BaseEvent g_grabPrelude{L"playerGrabbed_pre"};
BaseEvent g_thrown{P::name::PropThrownFn};
BaseEvent g_setPropProps{L"setPropProps"};

void* Resolve(BaseEvent& e) {
    if (e.missed) return nullptr;
    if (e.fn && e.cls.Alive()) return e.fn;
    e.cls.Set(R::FindClass(P::name::PropClass));
    if (!e.cls.Raw()) return nullptr;  // not loaded yet
    e.fn = R::FindFunction(e.cls.Raw(), e.name);
    if (!e.fn) {
        UE_LOGW("prop_events: %ls.%ls did not resolve -- a receiver does not replay it", P::name::PropClass,
                e.name);
        e.missed = true;
    }
    return e.fn;
}

}  // namespace

bool CallBaseGrabPrelude(void* prop) {
    if (!prop) return false;
    void* fn = Resolve(g_grabPrelude);
    if (!fn) return false;
    ParamFrame f(fn);  // the player and the hit result stay zeroed: the base body reads neither
    return f.valid() && Call(prop, f);
}

bool CallBaseSetPropProps(void* prop, bool isStatic, bool frozen, bool sleeping) {
    if (!prop) return false;
    void* fn = Resolve(g_setPropProps);
    if (!fn) return false;
    ParamFrame f(fn);  // `active` stays false: the base body never reads it
    return f.valid() && f.Set<bool>(L"static", isStatic) && f.Set<bool>(L"frozen", frozen) &&
           f.Set<bool>(L"sleeping", sleeping) && Call(prop, f);
}

bool CallCrowbarOpen(void* prop) {
    void* fn = prop ? R::FindDispatchFunctionCached(R::ClassOf(prop), L"crowbarOpen") : nullptr;
    if (!fn) return false;
    ParamFrame f(fn);  // pryingCrowbar stays null
    return f.valid() && Call(prop, f);
}

bool PryReplacesProp(void* prop) {
    void* cls = prop ? R::ClassOf(prop) : nullptr;
    void* box = ue_wrap::object_index::ClassByName(L"prop_dynamiteBoxClosed_C");
    if (!cls || !box) return false;
    return cls == box || R::IsDescendantOfAny(cls, &box, 1);
}

bool CallPropThrown(void* prop, void* player) {
    if (!prop || !player) return false;
    void* fn = Resolve(g_thrown);
    if (!fn) return false;
    ParamFrame f(fn);
    return f.valid() && f.Set<void*>(L"Player", player) && Call(prop, f);
}

}  // namespace ue_wrap::prop
