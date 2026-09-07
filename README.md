# GTA:SA GPS — 2.11.277 ARM64 port

This fork ports [AndroidModLoader/GTASA_GPS](https://github.com/AndroidModLoader/GTASA_GPS)
to the ARM64 Google Play regional build of GTA: San Andreas 2.11.277.

## Compatibility

- GTA: San Andreas 2.11.277 (`versionCode 3782367`)
- Package: `com.rockstargames.gtasager`
- Architecture: `arm64-v8a` only
- AndroidModLoader 1.2.1
- Verified `libGame.so` SHA-256:
  `f8f169add68200a2340f8d9fa082946a50983bc2aa2bc52b8d9c74d39afd8eb4`

The mod validates the target game instructions and required symbols before it
enables any GPS logic. It stays disabled when the game binary does not match.

## Download

Download the tested `libGPS64-gtasa-2.11.277-arm64.so` from the
[2.11.277 ARM64 release](https://github.com/rodekva/GTASA_GPS/releases/tag/v1.5.1-gtasa-2.11.277-arm64).

The release binary was tested on a Google Pixel 9 Pro XL. Its SHA-256 is:

`868a230a45e0dc14ed7aa9baed7cd126943487c35dece50cf12bba0eb4ad7a60`

## Installation

A working AndroidModLoader 1.2.1 installation is required. Rename the downloaded
library to `libGPS64.so` and place it at:

`/sdcard/Android/data/com.rockstargames.gtasager/mods/libGPS64.so`

This repository contains neither AndroidModLoader's APK integration nor any GTA
APK, game library, asset, save, or signing material.

## Port changes

- ARM64 hooks and runtime compatibility validation for 2.11.277
- Android 17-compatible lookup for `libGame.so`
- taxi/mission distance-rendering crash fix
- guarded mission, entity, pickup, and coordinate marker handling
- marker-matched route colours
- correct mobile OpenGL vertex colour ordering
- circular minimap route clipping

See [PORT_NOTES.md](PORT_NOTES.md) for build and implementation details.

## Credits and license

The original mod credits DK22Pac, JuniorDjjr, juicermv, and RusJJ. This is an
unofficial modified port and is not affiliated with Rockstar Games or Take-Two.
The existing zlib license and attribution are preserved in [LICENSE](LICENSE).
