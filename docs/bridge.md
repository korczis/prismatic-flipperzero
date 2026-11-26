# Host bridge

The app has no host interface of its own. Its contract with a computer is a folder on the SD
card, and the bridge, [`tools/bridge.py`](../tools/bridge.py), reads and writes that folder over
the Flipper's own RPC protocol. Nothing new runs on the device, and the app gains no input
surface. Why it is shaped this way is recorded in ADR 0005
(`.ai/repo/adrs/0005-host-bridge-is-a-host-cli-over-the-flipper-s-own-rpc-by-owne.md`).

```bash
just bridge status              # device, card space, the app's files, is the pack current?
just bridge status --md5        # the same, with the installed pack hashed on the device
just bridge push-pack           # install build/packs/kompendium.pkp (or a path), checked
just bridge pull build/pulled   # copy sessions.jsonl and progress off the card
just bridge sessions build/pulled/sessions.jsonl   # saved detector sessions, markers named
just bridge progress build/pulled/progress.bin build/pulled/progress.tmp   # Leitner boxes
```

`status`, `push-pack` and `pull` talk to the device through `tools/device.sh`, which holds the
device lock, so a bridge run never overlaps a deploy or a screenshot. `sessions` and `progress`
read pulled files and work without a device. Every command prints JSON except `push-pack`, which
prints one line of result.

## The folder

Everything the app reads or writes lives in `SD/apps_data/prismatic/`:

| File | Written by | What it is |
|---|---|---|
| `*.pkp` | the host (`push-pack`, `just deploy-pack`) | Knowledge packs, read-only for the app. Format: [architecture](architecture.md). |
| `progress.bin`, `progress.tmp` | the app, on every quiz answer | Quiz progress for spaced repetition, two copies. |
| `sessions.jsonl` | the app, only when the reader presses *Save these marks* | Saved detector sessions. |

The bridge writes only `*.pkp` files and never deletes or changes `progress.*` or
`sessions.jsonl`: those are the reader's own data, and Info on the device is where they are
forgotten.

### `sessions.jsonl`

JSON Lines, at most 32 KiB, one session per line, appended:

```json
{"v":1,"t":"2026-11-25T12:00:00","pack":"kompendium 1","c":[{"id":"...","i":173,"m":"OONUA"}]}
```

- `t` is the device clock's local time when Save was pressed; `pack` names the pack.
- `c` holds one or two candidates (two from a side-by-side detection): the object's stable `id`,
  its index `i` in the pack the marks were made with, and `m`, one letter per marker in the
  object's order: `O` observed, `N` not observed, `U` unknown, `A` not applicable, `-`
  unanswered.
- A line that does not parse (a torn last line after a lost write) is skipped by the app and by
  the bridge, which reports how many it skipped.

`bridge sessions` finds each object by its id (at its recorded index first, so a rebuilt pack
still resolves), names each marker, and counts the states. It never adds a score: absent
evidence is not contrary evidence.

### `progress.bin`

Little-endian, versioned, bounded and checksummed ([`progress.h`](../app/core/progress.h)):

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | `PKPS` |
| 4 | 2 | version, 1 |
| 6 | 2 | reserved, 0 |
| 8 | 4 | generation, bumped by every save |
| 12 | 2 | session, bumped by every start of the app |
| 14 | 2 | count of entries, at most 256 |
| 16 | 4 | reserved, 0 |
| 20 | 12 × count | entries |
| 20 + 12 × count | 4 | CRC-32 of everything before it |

An entry is the question's key (FNV-1a, 32 bits, of its prompt text, so it survives a rebuilt
pack), its index in the pack (2), its box 1 to 5 (1), whether the last answer was right (1), the
number of answers (2) and the session from which it is due again (2). The app saves through
`progress.tmp` and then replaces `progress.bin`; whichever valid copy has the higher generation
wins. `bridge progress` applies the same rule, names each question by its prompt from the pack,
and counts entries per box and those due now.

## The protocol

`status` and `pull` speak [flipperzero-protobuf](https://github.com/flipperdevices/flipperzero-protobuf)
over the USB serial port: the bridge opens the CLI, sends `start_rpc_session`, and from then on
every message is a varint length followed by a `PB.Main`. The encoder and decoder are the few
lines already used for screenshots ([`flipper_screen.py`](../tools/flipper_screen.py)); there is
no generated code and no dependency beyond pyserial.

| Request (`PB.Main` field) | Response | Used for |
|---|---|---|
| `system_ping_request` (5) | `system_ping_response` (6) | the session is ready |
| `system_device_info_request` (32) | `system_device_info_response` (33), one key per message | hardware name, firmware version |
| `storage_info_request` (28) | `storage_info_response` (29) | card space |
| `storage_list_request` (7) | `storage_list_response` (8) | the app's files and sizes; is the FAP installed |
| `storage_md5sum_request` (14) | `storage_md5sum_response` (15) | `status --md5` |
| `storage_read_request` (9) | `storage_read_response` (10), in parts | `pull` |
| `app_lock_status_request` (17) | `app_lock_status_response` (18) | is an app running |
| `stop_session` (19) | none | back to the CLI, only when no request is unanswered |

A session starts with `system_ping_request` (5), repeated until one is answered: on 1.5.1 the
first request after `start_rpc_session` can be lost, and an MD5 request sent first went
unanswered for ten minutes while the same request after a ping answered at once.

Each request has its own `command_id`; a response with `has_next` set is followed by more under
the same id, and a message with another id is skipped. A status other than OK is an error,
except "does not exist", where a missing file is an answer (nothing to pull, nothing installed).

A session is never ended while a request is unanswered: ending the session inside a long
storage request took the Flipper off USB twice on 1.5.1, and releasing the port ends the session
too. When a command gives up on an answer, the bridge still waits up to ten more minutes for it
before `stop_session`; only an answer that never comes leaves the port released with a warning
that the device may need a restart.

### Installing a pack, over the CLI only

`push-pack` does not use RPC at all. A first version mixed the two (RPC to prepare, the CLI to
write, RPC to verify); reopening the port for the CLI right after an RPC session ended left the
device deaf to both until it was restarted. So an install is one CLI session, the way uFBT's
`storage.py` sends files and `just deploy-pack` has always installed this pack:

| Command | Answer | Used for |
|---|---|---|
| `loader info` | `No application is running`, or the running app | refusing while an app may hold the pack |
| `storage md5 "<path>"` | the hash, or `Storage error: file/dir not exist` | before, after the write, after the swap (about 50 s for 20 MB) |
| `storage mkdir`, `storage remove`, `storage rename` | nothing, or `Storage error: ...` | preparing and swapping |
| `storage write_chunk "<path>" <size>` | `Ready`, then exactly `<size>` bytes, then the prompt | appending one 8 KiB chunk |

Every chunk is acknowledged before the next is sent, so the host never runs ahead of the card,
and a `Storage error` after the data (a full card) stops the push at once. The CLI echoes each
command line; the bridge skips the echo before it reads the answer, so a file name cannot pass
for one. A pack name is letters, digits, `.`, `_` and `-`.

1. The pack is decoded on the host with the reference reader first; a pack the app would refuse
   is never sent.
2. If an application is running, the push is refused: the app may have the pack open.
3. Unless `--force`, a pack whose MD5 on the device already matches is left alone.
4. A leftover `<name>.part` is removed (`write_chunk` appends), then the pack is written as
   `<name>.part`, chunk by acknowledged chunk.
5. The part's MD5 is computed on the device; on a mismatch the part is removed and nothing is
   replaced.
6. If an app was started during the upload, nothing is replaced and the verified part stays.
7. The old pack is removed, the part renamed, and the MD5 checked once more. If the rename is
   refused, the message names the part that holds the verified pack.

A pull, a status and a decode never write to the card.

## Tests and evidence

[`tests/tools/test_bridge.py`](../tests/tools/test_bridge.py) runs every command against a fake
device that speaks this subset of RPC (multi-part answers, stray messages with other ids,
refusals, an MD5 that does not match after the write, a rename that changes the file), and the decoders
against packs, sessions and progress images built in the test, including torn lines, a bad CRC,
an unknown version and the two-copy rule. The tools coverage gate requires every line of
`bridge.py` to run.

On the device (official 1.5.1, 2026-11-26; every run in
[`docs/evidence/bridge/manifest.json`](evidence/bridge/manifest.json), counts and hashes only):

- `status` and `status --md5` work: the device hashed the 20 MB pack in 50.4 s over RPC and the
  hash matched the local pack.
- `push-pack` works end to end for a small pack (9 500 bytes): installed, MD5 checked on the
  device, a second run reports it already current, `status --md5` agrees, and the device stays
  responsive throughout.
- `push-pack` of the 20 MB pack is **not** proven. Over the CLI every chunk was acknowledged, but
  a command after the upload (the device's MD5, then `loader info`, or removing a mismatched
  part) got no prompt within 60 s,
  and the device stopped answering until a restart. The installed pack was not touched: the
  swap comes later. Until this is understood, install the full pack with `just deploy-pack`.
- Three earlier attempts stopped the device too: an RPC session ended inside a long storage
  request (twice), and the port reopened for the CLI milliseconds after an RPC session ended.
  The bridge no longer does either.
- `pull` has not been run on the device yet.
