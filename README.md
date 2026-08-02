# Palworld Server Auto Revive

Palworld dedicated-server mod for Linux. It revives only the player's incapacitated Pals at three server-authoritative events:

1. immediately after one Pal is confirmed moved into that player's Palbox;
2. immediately after that player's respawn completes;
3. immediately after that player enters a base camp belonging to their own guild from outside.

The mod is server-side only. No client mod is required.

## Important status

This project targets the native Linux Palworld server through the community [UE4SS Linux native port](https://github.com/XarminaEu/ue4ss-linux). Palworld's official server-side mod documentation currently describes Windows dedicated servers, so the Linux path is community-supported and must be revalidated after Palworld updates.

The initial target server was Palworld Steam AppID 2394010, buildid 24445026, on Debian 13. The implementation is intentionally Lua-only so that the mod itself does not need a Windows DLL or a native C++ build.

### Debian target verification status (2026-08-02)

The repository was cloned to the target server and the MOD files are present. The save/config backup was completed before installation:

- archive: `/home/serveradmin/servers/palworld-backups/pre-mod-palworld-20260802T163237JST.tar.gz`
- SHA256: `3919104f73d16ff554e062272f91d63281c11f660dcffb8877e07d2ed1543ee5`

The target's Palworld build is `v1.0.2.100993`. The stable UE4SS Linux v3.0.2 library loads, but reports Linux limited mode and does not expose the UE hooks required by this MOD. The newer v3.0.26-linux-dev build reached full mode but then raised SIGSEGV during UE4SS initialization even with both Lua mods disabled, so it was rolled back. The target currently keeps `PalworldServerAutoRevive : 0` until a compatible UE4SS Linux build is available.


## Behavior

“瀕死” is deliberately narrow:

- the Pal is in the game's dying/incapacitated state;
- the Pal's HP is zero.

The mod calls the game's `ReviveFromDying()` path. It does not write HP directly and does not call `FullRecoveryHP()`. Therefore the recovery HP is chosen by the game's own revive implementation. Hunger, injuries, illness, SAN, and other status effects are not treated.

The Palbox event receives the exact `LastHandle` from the active-party slot update and verifies that the handle's current destination is the player's Palbox container. It does not scan every Palbox slot.

See [SPEC.md](SPEC.md) for the full requirements and acceptance criteria.

## Linux layout

The native Linux UE4SS port requires lowercase `scripts`:

```text
Pal/Binaries/Linux/
├── libUE4SS.so
├── UE4SS-settings.ini
└── Mods/
    ├── mods.txt
    └── PalworldServerAutoRevive/
        ├── README.md
        ├── SPEC.md
        └── scripts/
            ├── config.lua
            └── main.lua
```

## Debian installation

The repository is intended to be cloned directly as the UE4SS mod folder:

```bash
git clone https://github.com/p7672716/palworld-server-auto-revive.git \
  /home/serveradmin/servers/palworld/Pal/Binaries/Linux/Mods/PalworldServerAutoRevive
printf 'PalworldServerAutoRevive : 1\n' \
  >> /home/serveradmin/servers/palworld/Pal/Binaries/Linux/Mods/mods.txt
```

Install the Linux UE4SS base and the persistent systemd `LD_PRELOAD` drop-in before starting the server. The exact procedure, including the mandatory save backup and rollback, is in [docs/INSTALL-DEBIAN.md](docs/INSTALL-DEBIAN.md).

Do not start the server with the mod until a backup exists and the UE4SS archive checksum has been verified.

## Testing

The server log must first show:

- UE4SS loaded through `LD_PRELOAD`;
- this mod's `main.lua loaded`;
- all three hook registrations succeeded.

Then test each event with a disposable or already-backed-up world. The full test matrix is in [SPEC.md](SPEC.md).
