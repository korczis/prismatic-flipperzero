# First Light evidence (v0.0.1)

Recorded 2026-11-25 on a real Flipper Zero connected over USB to an Apple Silicon Mac
(macOS 26.5.1). Every image in this directory is a genuine frame read from the device's
RPC screen stream by `tools/flipper_screen.py`; none is drawn on the host.

## Device

| Field | Observed (`device_info` over the USB CLI) |
|---|---|
| firmware_version | 1.5.1 |
| firmware_commit | 9e810dc3 (not dirty) |
| firmware_api | 88.2 |
| radio stack | 1.20 |
| hardware target | f7 |

The device arrived in DFU (STM32 bootloader) mode. It was restored with the official
`qflipper-cli` 1.3.3 Full Repair on the release channel. Two attempts failed first: one
`qflipper-cli` crash (exit 139) after every flash stage had succeeded, and one USB
disconnect while the wireless stack was downloading. The third attempt completed every
stage (radio stack, firmware, option bytes, assets, region, restart). The SD card was
not erased.

## Build

| Field | Value |
|---|---|
| uFBT | 0.2.6 |
| SDK | official release 1.5.1, `flipper-z-f7-sdk-1.5.1.zip`, sha256 `dfde39df…2ffe` (pinned in `tools/sdks.json`) |
| API | 88.2, target f7 |
| warnings | 0 (clean rebuild) |
| `prismatic.fap` sha256 | `047b90683a191f09e345e518811084e510b683afe263682d68bfd2fbd0fd8c08`, identical after a clean rebuild |

## Runtime

| Claim | Evidence | State |
|---|---|---|
| Transfer | `ufbt launch`: `Sending … to /ext/apps/Tools/prismatic.fap` 100 %, exit 0 | VERIFIED |
| Launch | `loader info`: `Application "Prismatic" is running` | LOADER_CONFIRMED |
| Visible | [`splash.png`](splash.png) | VISUAL_CONFIRMED |
| Navigation | [`walk/00-start.png`](walk/00-start.png) → Right (Continue) → [`walk/01-right.png`](walk/01-right.png) shows `Firmware 1.5.1`, read from the device at runtime → Back → [`walk/02-back.png`](walk/02-back.png), the desktop | VERIFIED |
| Graceful exit | after Back, `loader info`: `No application is running` | VERIFIED |

The first `ufbt launch` reported success, but a `loader info` sent right afterwards said
`No application is running`. `loader open /ext/apps/Tools/prismatic.fap` then started the
app and the loader confirmed it. Later `just launch` runs showed the app on screen at once
(the frame was captured right after). The first observation is recorded as it happened.

## Reproduce

```bash
just launch                                   # build, install, start
just screenshot build/screen.png              # one real frame
just walk build/walk right,back               # navigate and capture each step
```
