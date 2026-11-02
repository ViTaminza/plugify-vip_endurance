# vip_endurance (Plugify)

[VIP] Endurance: every frame, sets m_flVelocityModifier back to 1.0 for alive VIPs with the togglable 'Endurance' feature (cancels the hurt slowdown). Ported from the Pisex VIP core module VIP_Endurance (vip_endurance). Requires the vip core.

Plugify port (fork) of **VIP_Endurance ([VIP] Endurance)** by Pisex (Metamod (C++)).
Original: https://github.com/Pisex/cs2-vip-modules
The code was rewritten for Plugify / s2sdk.

## Requirements
- [Plugify](https://github.com/untrustedmodders/plugify) with [s2sdk](https://github.com/untrustedmodders/plugify-plugin-s2sdk)
- Plugin version: 1.1.0
- Plugify dependencies: s2sdk (>=2.18.2), vip (>=1.0.0)
- Self-contained s2sdk integration: uses s2sdk directly, without the pisexcompat library.

## Installation
Download `vip_endurance-1.1.0.zip` from Releases and extract it into `game/csgo/`.

## Building
Requires Docker. `./build.sh` builds in the Steam Runtime 3 (sniper) SDK image; the package
is written to `build/package/vip_endurance`.

`plugins/vip` carry only the API headers of the plugins this one uses.

## License
GPL-3.0, see LICENSE.

