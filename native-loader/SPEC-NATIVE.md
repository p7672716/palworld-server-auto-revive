# Debian native loader layer specification

## Scope

This document specifies the Linux-native C++ layer for the Palworld server
auto-revive MOD. It is an implementation companion to the user-facing MOD
specification in the repository root.

The layer is server-only. It must run in the existing native Debian process,
use the existing save directory unchanged, and avoid a client-side MOD.

## Runtime contract

- Hook boundary: UE `ProcessEvent` pre/post callbacks.
- Script runtime: disabled; no Lua startup is required.
- Target ABI: the exact Palworld executable and custom Linux UE4SS build used
  during compilation.
- Threading: callbacks execute on the game thread supplied by the hook.
- Failure policy: if a reflected function, return layout, party source, or
  guild identity cannot be resolved, skip the operation and emit a bounded
  diagnostic; never guess an HP value or scan the Palbox.

## Downed definition and action

An individual is eligible only when the same parameter component reports both:

- `IsDying() == true`
- `IsDyingHPZero() == true`

The action is a no-argument `ReviveFromDying()` call on that component. The
layer does not set HP directly and does not call an API that requires a
hard-coded recovery amount.

## Event mapping

### Palbox move

The pre-callback handles `SetOtomoSlot` (and its server variant). It resolves
the player and exact slot, stores only the previous party object for that slot,
and the post-callback applies the downed check to that stored object. No box
object is enumerated. A slot index outside 0..4 is ignored.

The authoritative event name and parameter layout must be revalidated after a
Palworld update; a generic party reorder must not be accepted as a Palbox
move without that validation.

### Respawn completion

The post-callback handles `CallRespawnDelegate` and `RespawnReady`. It resolves
the player and queries only `GetOtomoActorBySlotIndex(0..4)`, reviving eligible
objects from those five slots.

### Own-guild base entry

The post-callback handles `OnPlayerEnterBaseCamp` and `OnEnterBaseCamp`. It
extracts the player/base-camp arguments for the known Palworld signatures,
then requires a positive guild identity match. Pointer identity is preferred;
16-byte group/guild IDs are accepted only when both sides return a non-zero,
matching ID. Unknown identity is a skip, not an allow.

## Performance constraints

- No per-`ProcessEvent` logging in the steady state.
- No global object scan.
- No Palbox scan.
- At most five party lookups for respawn/base-entry handling.
- The Palbox path performs one slot lookup and one downed check for the moved
  object.

## Compatibility and rollback

The native `.so` must be rebuilt whenever the Palworld executable, reflected
function signatures, UE4SS headers, or member layout changes. Before replacing
the loader or MOD, stop the service and create a save/config backup. If the
service restarts, emits a segmentation fault, or any event test is ambiguous,
restore the previous `libUE4SS.so`, `mods.txt`, and native module, then start
the service with the MOD disabled.


