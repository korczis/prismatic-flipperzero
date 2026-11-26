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

The bridge speaks [flipperzero-protobuf](https://github.com/flipperdevices/flipperzero-protobuf)
over the USB serial port: it opens the CLI, sends `start_rpc_session`, and from then on every
message is a varint length followed by a `PB.Main`. The encoder and decoder are the few lines
already used for screenshots ([`flipper_screen.py`](../tools/flipper_screen.py)); there is no
generated code and no dependency beyond pyserial.

| Request (`PB.Main` field) | Response | Used for |
|---|---|---|
| `system_ping_request` (5) | `system_ping_response` (6) | the session is ready |
| `system_device_info_request` (32) | `system_device_info_response` (33), one key per message | hardware name, firmware version |
| `storage_info_request` (28) | `storage_info_response` (29) | card space |
| `storage_list_request` (7) | `storage_list_response` (8) | the app's files and sizes; is the FAP installed |
| `storage_md5sum_request` (14) | `storage_md5sum_response` (15) | checking a pack, before and after a push |
| `storage_read_request` (9) | `storage_read_response` (10), in parts | `pull` |
| `storage_mkdir_request` (13), `storage_delete_request` (12), `storage_rename_request` (30) | status | `push-pack` |
| `app_lock_status_request` (17) | `app_lock_status_response` (18) | refusing to push while an app runs |
| `stop_session` (19) | none | back to the CLI, only when no request is unanswered |

A session starts with `system_ping_request` (5), repeated until one is answered: on 1.5.1 the
first request after `start_rpc_session` can be lost, and an MD5 request sent first went
unanswered for ten minutes while the same request after a ping answered at once.

Each request has its own `command_id`; a response with `has_next` set is followed by more under
the same id, and a message with another id is skipped. A status other than OK is an error,
except "does not exist" where a missing file is an answer (nothing to pull, nothing installed)
and "already exists" for `mkdir`.

A session is never ended while a request is unanswered: ending the session inside a long
storage request took the Flipper off USB twice on 1.5.1, and releasing the port ends the session
too. When a command gives up on an answer, the bridge still waits up to ten more minutes for it
before `stop_session`; only an answer that never comes leaves the port released with a warning
that the device may need a restart.

Before the swap the lock is checked again (an app started during the upload leaves the verified
pack as `.part` and replaces nothing), and if the rename itself is refused the message names the
`.part` file that holds the verified pack. A pack name is letters, digits, `.`, `_` and `-`.

The pack itself goes over the text CLI, not RPC: `storage write_chunk "<path>" <size>` answers
`Ready` (or `Storage error: ...`), takes exactly `<size>` bytes, appends them and answers with the
prompt. Every 8 KiB chunk is acknowledged before the next is sent, so the host never runs ahead
of the card; this is how uFBT's `storage.py` sends files, and how `just deploy-pack` has always
installed this pack. The bridge skips the echoed command line before it looks for the answer, so
a file name cannot be mistaken for one.

### Installing a pack

1. The pack is decoded on the host with the reference reader first; a pack the app would refuse
   is never sent.
2. If an application holds the device, the push is refused: the app may have the pack open.
3. Unless `--force`, a pack whose MD5 on the device already matches is left alone.
4. A leftover `<name>.part` is deleted, and the RPC session is ended cleanly.
5. The pack is written as `<name>.part` over the CLI, chunk by acknowledged chunk.
6. A new RPC session computes the part's MD5 on the device; on a mismatch the part is deleted
   and nothing is replaced.
7. The old pack is deleted, the part renamed, and the MD5 checked once more.

A pull, a status and a decode never write to the card.

## Tests and evidence

[`tests/tools/test_bridge.py`](../tests/tools/test_bridge.py) runs every command against a fake
device that speaks this subset of RPC (multi-part answers, stray messages with other ids,
refusals, an MD5 that does not match after the write, a rename that changes the file), and the decoders
against packs, sessions and progress images built in the test, including torn lines, a bad CRC,
an unknown version and the two-copy rule. The tools coverage gate requires every line of
`bridge.py` to run.

On the device (official 1.5.1, 2026-11-26):

- `status` and `status --md5` work: the device hashed the 20 MB pack in 50.4 s over RPC (the CLI's
  `storage md5` took 50.3 s) and the hash matched the local pack.
- A push of the 20 MB pack sent every part (4 min 47 s) but the write's final status did not
  arrive within the 60 s the bridge then waited; the bridge ended the session, and the Flipper
  dropped off USB. The installed pack was not touched: the failure came before the rename.
- The first incident had the same shape: an MD5 request that the bridge stopped waiting for after
  120 s, the session ended, the Flipper gone from USB. Ending an RPC session while the device is
  still inside a long storage request is what both have in common; the bridge must not do it.

So `push-pack` is not yet proven on the device, and the evidence for it is not recorded. What
remains: wait for the write's status as long as the device needs (or move the transfer to the
storage CLI, which `just deploy-pack` uses for the same pack without trouble), run `push-pack` and
`pull`, and record the run under `docs/evidence/bridge/` as counts and hashes only: what the
reader saved stays on the reader's side.
