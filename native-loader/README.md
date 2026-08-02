# Palworld native Linux mod loader

This directory contains the Debian-native C++ bridge used by the server. It
loads through the custom Linux UE4SS C++ path, but deliberately does not start
Lua or the generic UE4SS UE-property lifecycle. The bridge uses only the
`ProcessEvent` pre/post callbacks and Palworld's reflected functions.

The current bridge implements these guarded paths:

1. `SetOtomoSlot` captures the individual in the affected party slot before
   the authoritative move and checks only that individual afterward.
2. `CallRespawnDelegate`/`RespawnReady` scans only the five active party slots.
3. `OnPlayerEnterBaseCamp`/`OnEnterBaseCamp` scans the party only after a
   positive own-guild identity comparison.

Every revive requires both `IsDying` and `IsDyingHPZero`, then calls the
game-native `ReviveFromDying` function. It does not write HP, calculate a
maximum HP, inspect the Palbox, or treat hunger, illness, injury, or SAN as
the revive condition.

The project is built against the exact UE4SS source/build and Palworld binary
used by the target server. The `.so` is therefore not portable: rebuild and
re-run the compatibility checks after a Palworld or loader ABI update.

The first production gate is still an in-game event test. Until all three
paths are observed with a disposable/test save, keep the native MOD disabled
and retain the pre-install save backup.


