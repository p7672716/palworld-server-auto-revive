# Palworld Server Auto Revive

Palworld dedicated-server mod for Linux. It revives only the player's incapacitated Pals at three server-authoritative events:

1. immediately after one Pal is confirmed moved into that player's Palbox;
2. immediately after that player's respawn completes;
3. immediately after that player enters a base camp belonging to their own guild from outside.

The mod is server-side only. No client mod is required.

## Important status

This project targets the native Linux Palworld server through the community [UE4SS Linux native port](https://github.com/XarminaEu/ue4ss-linux). Palworld's official server-side mod documentation currently describes Windows dedicated servers, so the Linux path is community-supported and must be revalidated after Palworld updates.

The initial target server was Palworld Steam AppID 2394010, buildid 24445026, on Debian 13. The implementation is intentionally Lua-only so that the mod itself does not need a Windows DLL or a native C++ build.

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
