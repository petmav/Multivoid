# Vehicles

## Purpose

The ATV, the game's one vehicle: a five-body physics rig with a seat that takes the player's
pawn, three passenger seats the mod adds beside it, a parts economy of modules, tyres and a spare, and
accumulators for fuel, dirt and health.
How the peer driving it and the peers watching it share one rig, why a watching peer runs the
rig natively instead of freezing it, what condition travels, and what is still open.

## How it works

### The rig, and why a mirror is not frozen

The ATV is not an engine vehicle: its root body simulates physics and its four wheels are
separate simulating bodies held by constraints, so the vehicle's visible output is suspension
travel, and one actor transform is not the pose. The first lane froze a mirror and teleported its
root twenty times a second, dragging four constrained bodies behind it: measured suspension
travel of thirty centimetres against a native two to four, and a release that launched the other
peer's copy at a metre and a half per second. That lane was replaced whole
(`coop/interactables/atv_sync`).

A peer that does not sync an ATV runs the rig natively, physics on and tick on, and is
steered toward the syncer on every packet (`coop/interactables/atv_corrector`): the
velocity is written, the position error is closed by a bounded corrective velocity, and past a
speed-scaled threshold, or when the error stops shrinking, the mirror is cut to the syncer's
pose through the game's own teleport. This is MTA's vehicle shape, and the one thing a mirror may
not do is author collision damage: the seven hit delegates are intercepted on a peer that does
not own the rig's tick and dispatched with a zeroed impulse, so the notification runs whole and
only the damage dies (`coop/interactables/atv_hit_guard`). Cancelling the wheel delegates
outright cost a mirror its rig shape, a sag of twenty-five to forty centimetres, until the cause was
found.

### The seat and the syncer are the host's

Who sits and who writes the rig are different questions, and the host decides both
(`coop/interactables/atv_authority`); no machine decides either for itself. The seat is a claim: a
client's local sit sends `AtvSeat`, and the host judges it (the sender's world ready, the ATV known,
the sender alive, a pace, one seat at a time, the seat free, the sender within reach) and answers
every request with `AtvAnswer`. The host's own player is judged by the same table at its own tick,
without the reach test. The syncer is the peer whose copy writes the rig and runs the brain, the
accumulators, wheel torque and collision damage. It is the occupant while seated; unseated, it stays
with the peer that drove or carried the ATV until that peer is out of range (130 m) or silent for
five seconds, or the ATV has been at rest and uncarried for a second, and then the host takes it
back. A grab sends `AtvTakeover`, judged the same way and refused while the ATV is seated or another
peer carries it. A peer merely carrying an ATV syncs it but is not its occupant, so it never denies
the seat. A client acts at once on its local sit or grab and yields to the answer: a refused sit
dismounts through the game's own dismount, a refused grab drops. Every machine follows the host's
stamp, the seat and the syncer on each `AtvState` the host sends. `[?]` Built to this design; no
two-peer run has judged it yet.

### The passenger seats

The game's ATV seats one player, and sitting possesses the rig. A full session is four players, so the mod adds
three passenger seats (`coop/interactables/atv_passenger`, `ue_wrap/devices/atv_passenger`). A passenger does not
possess anything: its own player stays possessed and is attached to the rig's root body at a seat, its movement mode
None so no key moves it, its collision off so it cannot push the rig, its camera and mouse-look its own, and the
driver's controls untouched. The engine moves an attached body with the rig each frame, so a passenger rides it
instead of trailing a stream behind it.

**Boarding.** The use key at an ATV whose driver's seat another player holds, which was denied before, boards the
first free passenger seat when the sit verb's own gates pass (empty hands, not falling). The press is a
PRE observer on the player's use input (`coop/interactables/device_occupancy`), the seam that already denied it: the
game's sit verb is not run (the aim is cleared for that dispatch), so the trunk's open option, the driver's seat on an
empty ATV and the grav-hand grab are untouched. A press with every seat taken, or a hand that is full, is denied as before.

**Leaving.** The use key again (the press is spent: nothing else answers it), or the jump key (Space, read as a key edge
while the window is the game's and no text field has the keyboard). The player is put beside the ATV: the seat's own
side first, then the other side and the rear, the first spot with no wall between it and the rig, ground under it
(found by a trace down) and headroom, else the NavMesh point nearest the rig. A leave also happens by itself when the
ATV's health reaches zero or its actor goes, when the player dies or is ragdolled, when anything detaches the body or
moves it more than 1.5 m from its seat (every teleport of the body, the cheat panels' and Unstuck included, runs through
`teleport_client::ApplyLocally`, which leaves the seat first), and at a world change or the session's end.

**Seats.** Three offsets in the rig's root space (X forward, Y right, Z up, cm) of the rider's capsule centre, the position a
pose stream carries, so every machine seats a rider at the same place. They are measured on the game's ATV (the drill's `geom` lines:
the rig's bounds, its components, and a trace down over a grid): behind the driver's seat, whose top is at z 71, the rig has a flat rear
deck at z 56 to 62 from x -40 to -130 and wider than y +-60, and a seated body's centre rides 19 above what it sits on (the driver's is
at 90), so the three seats are abreast across the deck, 19 above its surface: (-80, 0, 75), (-80, -45, 80) and (-80, 45, 80). The
game has no sitting pose for a player, so a rider is the standing body sunk to the hips into the deck, as the driver is in the seat. The driver leaving ejects nobody: the driver's
seat and the passenger table are separate state.

**The table is the host's.** `AtvEntry::passengerSlot[3]` holds the peer slot in each passenger seat, written on the
host by judgement and on a client by the host's `AtvPassengerTable`. A client acts at once on its own boarding, at the
seat its own table copy shows free, and asks the host with `AtvPassengerRequest`; the host judges in this order: the
sender's world ready, the request well formed, the ATV known, a leave from a rider (frees its seat, takes no token), then
a board: the sender's body alive and present, a pace (four boards, one more each second), the sender seated nowhere else (a
driver's seat and a passenger seat both count, so a driver or a rider asking again is refused, and a passenger's driver sit is
refused too), a free seat, and within reach of the ATV by the movement ledger's accepted position. Every judged request is answered with
`AtvPassengerAnswer` carrying the seat the sender now holds there; a refused board makes the client leave beside the ATV, and the
client's table copy follows the answer. The host's own player is judged by the same table at the press, without the reach test or the
pace. The table is sent to every world-ready client when it changed and to a joiner after its ATV state at the connect replay (a table
that reaches a client before its ATV's row waits for the row). A departure frees that slot's seats, and the host's once-a-second pass
takes out a rider whose body is dead, or whose accepted position stays more than 15 m from its seat for two seconds (a machine that
left the rig and could not say so; a client's copy of the rig trails the host's, so the bound is wide, and a teleport the rider's own
machine saw has sent its leave already).

**The remote view.** Every machine seats each remote rider's puppet at its seat on its own copy of the ATV: attached to the
copy's root body, collision off, its relative place rewritten after the puppet's pose drive each frame (the stream still
turns its head and body). A seated puppet shows no locomotion: the stream's speed is the rig's, so `RemotePlayer::SetSeated`
holds the legs, the fall and the footsteps still. When the seat clears the puppet is let go and follows the pose stream
again. The pose stream itself is unchanged: a rider's own body is attached to its copy, so it streams the seat's position, at
speeds up to the ATV's turbo, which the movement ledger already trusts (its 10 000 cm/s bound); the board is the one
discontinuity, a hop of the distance to the seat, which the ledger charges as a short debt it repays in milliseconds, and which
only logs (it is measure-only).

`[V]` the passenger drill (`coop/dev/atv_passenger_drill`): a client boards through the real use key and holds seat 0 on both tables,
the host's puppet of it attached to the host's copy; the host drives and the client's body stays within a few centimetres of the seat
on its own copy and the puppet on the host's; the driver leaves and the passenger stays; the client leaves beside the ATV, on the
ground; the host's own player rides the client's ATV the same way, its puppet seated on the client's copy; a second boarding, left by
the F1 Unstuck teleport; and the host's refusals of a duplicate board, a board with every seat taken and a board out of reach by its
counters. `[?]` Not driven: three players (a third machine's puppet seat, and the connect replay of a table to a joiner, which a two-peer
drill cannot reach), a passenger's death or an ATV's explosion with riders aboard.

### The stream

While synced by a peer, the rig's pose, full rotation, linear and angular velocity stream about
twenty times a second, keyed by the ATV's game key. A client's stream reaches the host alone: the
host applies only its syncer's blocks, paced, and re-sends each to the other clients with its stamp.
An idle ATV is
synced by the host at five hertz gated on change with a two-second keepalive floor, so a parked
vehicle costs one packet every two seconds. An ATV that appears at runtime, from the spawn menu,
has no save twin, so the host announces it under its game key.
The client parks a valid announcement until gameplay, the discovery index and load quiescence
agree, stamped with the world it arrived in: a world change drops it, the new world's own replay
bringing it again, and a spawn that fails in a ready world is final, never retried.
`[?]` This readiness path still needs a two-peer join and travel test.

### Condition

The syncer's condition rides the same payload: per-wheel durability, dirt, fixes and type, the
spare's trio, body dirt, fuel, health and the battery's charge (`coop/interactables/atv_condition_sync`). A mirror is
overwritten and re-derives its visuals through the game's own reducers on change edges, against a
baseline seeded from the actor rather than from the last packet: a zero baseline would break all
eight constraints on a settled rig, and a per-packet one would starve the dirt reducer forever.
Accumulators apply from the syncer, clamped to the game's domains when it is a client; presence,
which tyres are on, whether the spare is and whether a battery is fitted, is consumed only from the
host's own copy, because a client's part births cannot travel, and applying a client syncer's mask
would turn a divergence into an item loss the host persists. The mirror's own accrual is held at zero by the impulse neuter, so an
overwrite never races an irreversible act. The battery's charge is compared with the copy as it
stands, not with the baseline, and `updBattery` runs when the back lights' test changes.

### Parts

A wrench or power tool unbolts a wheel or the battery, which leaves as a prop; a held wheel goes
back into a socket or the spare box, a held battery into an ATV with none, and the spare comes out
of its box by the radial option. Each of these changes presence, so on a client the body that would
make the change is cancelled at the script gate and asked of the host with `AtvPartIntent`
(`coop/interactables/atv_part_intent`): `unscrewPanel` for a looked-at wheel or the battery,
`putTire` for a fit, `playerUsedOn` for the spare's stow and the battery's insert, and
`actionOptionIndex` for the spare's take. A fit, stow or insert destroys the part in the client's
hand as the body would and carries the part's scalars. The host judges the sender's world, the ATV
by its key, that the host itself syncs it, the reach, the tool or the part its hand held last (one
hand state gives one part) and the ATV's own state. An unbolt then runs the game's own
`unscrewPanel` on the host's copy with the look fields set for the call; a fit, stow or insert
writes the fields the body writes and runs its reducer; the spare is born as a wheel prop beside
the sender. The part's birth crosses as the host's and presence follows in `AtvState`. A refused
fit, stow or insert gives the part back as a prop beside the sender. `[V]` `coop/dev/atv_part_drill`
(`VOTVCOOP_ATV_PART_DRILL=wheel|battery`, both peers): the client, a wrench in its hand, unbolts a
wheel or the battery and fits a held one back; the host ran both asked changes and refused none, and
after each both peers held the same tyre mask, battery and part props. The spare's stow and take
have no drill `[?]`.

### The trunk

In 0.10 an ATV's trunk is its own `inventoryComponent_C` (the open option runs the inventory screen's `begin(atv.inventoryComponent)`);
the `spawnedContainer` prop of 0.9 is never created. The component is the host's: `coop/interactables/atv_trunk_sync` reads each
ATV's trunk twice a second, sends it whole when it changed (`AtvTrunk`, a chunked blob in the container slice grammar, host to client)
and a client writes it over its copy; a joiner gets every trunk at its connect replay. The trunk is keyed by the ATV's game key (a
target id of the container act lane: a base plus 20 bits of a hash of the key), and a client's take, put, stack and merge on the open
trunk are asked of the host like a world container's, with reach judged at the ATV. A client's hand put is refused. Details:
[props.md](props.md).

## Who owns what

| State | Owner | Shape |
|---|---|---|
| the pose while driven or carried | the syncer | a twenty-hertz stream; a client's reaches the host, which re-sends it stamped |
| the pose while idle | the host | five hertz on change, a keepalive floor |
| the seat | the host | a judged request, answered, stamped to every client |
| the passenger seats (three) | the host | a judged request, answered, the table sent to every client and replayed to a joiner |
| the syncer | the host | assigned on a sit or a grab, taken back at rest, out of range or on silence |
| collision damage, the accumulators, wheel torque | the tick owner, the syncer's machine | a mirror's impulses are zeroed |
| every route to an ATV's damage and death (the two blast verbs, the explosion) | the tick owner | a copy that does not own the tick cancels them at the script gate; the owner takes every blast that reaches it |
| the story event's trap | the host | the host copy's `trap` rides the stamp (bit 4); the driven copy fires it |
| the condition values | the syncer | overwritten on the mirror, re-derived on edges |
| tyre, spare and battery presence | the host | consumed from the host's own copy only; a client's unbolt, fit, stow, insert and spare take are asked of the host |
| a runtime ATV's identity | the host | its game key, announced |
| the trunk's contents | the host | the host's component is the truth; a client's acts are asked and judged there |

## Wire messages

| Kind | Direction | Carries |
|---|---|---|
| `AtvState` | the syncer (a client's to the host alone), re-sent by the host; the host when idle | key, pose, rotation, velocities, the seat and the syncer the host stamps, the host copy's trap, the condition block, an adopt flag |
| `AtvSeat`, `AtvTakeover` | a client to the host | a sit in or leave of the driver's seat; a grab of the ATV |
| `AtvAnswer` | the host to the asking client | the verdict on one request, with the seat and the syncer the host holds |
| `AtvExplodeRequest` | a client to the host | the ATV the client syncs that exploded, by key, and whether the story event's trap fell in that frame; the host blasts at the ATV's own `tp` |
| `AtvPassengerRequest` | a client to the host | a board of the first free passenger seat, or a leave of the one held |
| `AtvPassengerAnswer` | the host to the asking client | the verdict on one request, with the seat the sender holds on that ATV |
| `AtvPassengerTable` | the host to every ready client; a joiner at its replay | one ATV's passenger table: the slot in each of the three seats |
| `AtvPartIntent` | a client to the host | one piece of work on an ATV's parts: a wheel or the battery unbolted, a held wheel fitted or stowed as the spare, a held battery put in, the spare taken; with the held part's scalars |
| `AtvSpawn`, `AtvDestroy` | the host to all | an ATV's key, class and pose; its teardown |
| `AtvSetComplete` | the host to a joiner | the count of distinct keys its connect snapshot named |
| `AtvTrunk` | the host to all | one ATV's trunk entries, whole, as a chunked blob in the container slice grammar (element id = the trunk's target id) |

## Late join

The joiner loads the host's save first, so it already holds the ATVs the save placed. Once its
save has finished loading, the host walks every ATV its world holds and announces each by key, then
closes the set with `AtvSetComplete`. The joiner adopts the copy it holds under that key and spawns
a mirror only when it holds none, so it ends with exactly one actor per host ATV. It then removes
every ATV the set did not name, such as one the host lost after the save was taken. Arrivals that
come before the joiner's world is ready wait in a park. A set the host cannot vouch for (an ATV
still loading, a send that failed) is withheld, which removes nothing; the host owes it and
retries. Each ATV's pose, velocity, condition and the host's stamp (the seat and the syncer) follow,
so an ATV airborne at the join arrives moving and lands, and the joiner's mirror is cut to the pose
on its first packet, even for an ATV a client syncs; after that it follows the syncer's blocks. A
joiner sends no request before its own world is ready: a sit or a grab it makes before then is held,
not yielded, and goes out at that point. A rejoin into the same slot is a departure and an arrival,
and the leaver's seat and syncerships were cleared at its departure. `[V]` the ATV spawn drill's
run, join, park and divergence arms, each red arm failing as designed (`coop/dev/atv_spawn_drill`).

## Known limits

| Limit | Evidence |
|---|---|
| While driving, the mirror trails the syncer: the correction is a bounded velocity bias, and the cut to the syncer's pose happens only past a speed-scaled error or when the error stops shrinking | `[V]` `coop/interactables/atv_corrector` |
| A client drives for one round trip before the host's answer can refuse its sit or grab; a refused sit dismounts, a refused grab drops, and the blocks it streamed meanwhile are dropped and counted, never applied | `[?]` `coop/interactables/atv_authority`, built to the design and not yet run |
| At a hand-back to the host the old syncer's last blocks may still be in flight; the host drops and counts them, and the stamp moves every client to the host's copy | `[?]` `coop/interactables/atv_authority`, `coop/interactables/atv_sync` |
| A wheel knocked off by damage on a client syncer's copy is not consumed, so the host keeps the wheel and the peers disagree | `[V]` `coop/interactables/atv_condition_sync`, by design |
| A client's part work is refused while a client syncs the ATV (it is driven or carried, or not yet handed back): presence reaches the clients only from the host's own copy while the host syncs. The refused ask shows the client nothing but its part given back | `[?]` `coop/interactables/atv_part_intent` |
| The spare a client takes is born as a prop beside it, not into its hotbar; an unbolt the host runs for a client plays the game's 2D sounds on the host | `[?]` `coop/interactables/atv_part_intent` |
| The pose stream rides the reliable lane; a sequenced unreliable stream is the intended shape | `[V]` `coop/interactables/atv_sync` sends it reliable |
| Not synced: the thirteen modules and every upgrade effect, lights, brake and turbo, honk, the radio, the map, repair, the kerfur passenger, and a remote driver's body is not seated on the ATV | `[V]` nothing under `coop/interactables/atv_*` carries them |
| A passenger has no sitting pose: the game has none for a player, so the puppet shows the idle stand sunk into the deck, and the jump key that leaves is Space whatever the game's rebinding. The seat offsets were measured by trace and bounds and checked by eye in the drill's noon screenshots of the rig from its side and rear | `[V]` `coop/interactables/atv_passenger` |

## Code map

| Concept | Files |
|---|---|
| the lane | `coop/interactables/atv_sync`, `coop/interactables/atv_sync_internal.h`; `coop/interactables/atv_body` (this machine's own seat and carry against the table) |
| the seat and syncer table, and the host's judgements | `coop/interactables/atv_authority` |
| the corrector, the hit guard and the damage gate | `coop/interactables/atv_corrector`, `coop/interactables/atv_hit_guard`, `coop/interactables/atv_damage_gate` |
| condition | `coop/interactables/atv_condition_sync`, `ue_wrap/devices/atv_condition` |
| a client's part work | `coop/interactables/atv_part_intent`, `ue_wrap/devices/atv_parts`, `coop/dev/atv_part_drill` |
| the trunk | `coop/interactables/atv_trunk_sync`, `ue_wrap/devices/atv` (`InventoryOf`, `ReadTrunk`, `WriteTrunk`), `coop/props/container_target` (the id), `coop/props/container_reach` |
| the engine wrapper | `ue_wrap/devices/atv` |
| the seat | `coop/interactables/device_occupancy` |
| the passenger seats | `coop/interactables/atv_passenger` (this machine's rider, the table's wire and the puppets' seats), `coop/interactables/atv_authority` (the table and its judgements), `ue_wrap/devices/atv_passenger` (the seat offsets, the attach, the exit spot), `coop/dev/atv_passenger_drill` |
| tests and probes | `coop/dev/atv_probe` (the drive arm), `coop/dev/atv_tire_probe`, `coop/dev/atv_eject_drill`, `coop/dev/atv_trunk_drill` (a client's take and put, the host's own add); the smoke run with the ATV scenario enabled |
