# GTA:SA GPS 2.11.277 ARM64 port

This branch ports GTA:SA GPS to the Google Play regional package
`com.rockstargames.gtasager`, version `2.11.277` (`versionCode 3782367`).

The target `libGame.so` used for this port has SHA-256:

`f8f169add68200a2340f8d9fa082946a50983bc2aa2bc52b8d9c74d39afd8eb4`

The port is ARM64-only and requires AndroidModLoader 1.2.1. It validates the
target instructions and required exported symbols before enabling any GPS
logic. If the binary does not match, the mod logs an error and stays disabled.

Android 17 may prevent AML 1.2.1's legacy library lookup from finding a game
library mapped directly from an APK. This port falls back to
`dl_iterate_phdr()` and uses an in-range ARM64 branch island for the path-node
call hook, avoiding AML 1.2.1's out-of-range `HookBLX` trap.

Version 1.5.1 also contains the runtime fixes found during device testing:

- calls the ARM64 mobile `CFont::SetColor` export with the pointer ABI expected
  by 2.11.277, fixing the immediate taxi/mission-distance crash;
- resolves coordinate, vehicle, character, object, pickup, contact-point,
  spotlight, and airstrip radar traces with null, handle, and coordinate guards;
- derives the route colour from its marker (with `UseMarkerColors=true` by
  default), including a red player waypoint and green/yellow mission markers;
- uses the OpenGL vertex colour byte order expected by the mobile renderer; and
- clips the in-game route geometry to the circular minimap instead of relying
  on the legacy rectangular widget scissor used by older releases.

The build was installed and functionally tested on a Google Pixel 9 Pro XL:
GTA loaded a restored save, accepted a player waypoint, rendered its red
road-following route on the full map and inside the circular minimap, displayed
the distance meter without a crash, and remained running through taxi mission
state changes.

## Building

Use Android NDK r25c and the AndroidModLoader 1.2.1 `mod` headers/sources:

```powershell
$project = (Get-Location).Path -replace '\\','/'
& '<ndk-r25c>/ndk-build.cmd' `
  "NDK_PROJECT_PATH=$project" `
  "APP_BUILD_SCRIPT=$project/Android.mk" `
  "NDK_APPLICATION_MK=$project/Application.mk" `
  -j4
```

The output is `libs/arm64-v8a/libGPS64.so`.

## Installation layout

AndroidModLoader must be loaded after `libGame.so`. Place the built mod at:

`/sdcard/Android/data/com.rockstargames.gtasager/mods/libGPS64.so`

The APK loader integration and any original game files are intentionally not
included in this source tree.
