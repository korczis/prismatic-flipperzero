# Security

Report vulnerabilities privately through
[GitHub private vulnerability reporting](https://github.com/korczis/prismatic-flipperzero/security/advisories/new).
Please do not open a public issue for a vulnerability.

Scope and boundaries:

- The FAP performs no radio transmission or network access and contains no credentials.
  It writes only under `SD/apps_data/prismatic/`: quiz progress (`progress.bin`, at most
  3 KiB, saved through `progress.tmp`) and, when the reader saves a detector's marks,
  `sessions.jsonl` (at most 32 KiB).
- The knowledge pack is untrusted input: every offset is bounds-checked, the reader is fuzzed
  with mutated packs under ASan and UBSan, and a corrupted record shows "could not be read" on
  its screen.
- Release `SHA256SUMS` detect accidental corruption. They are not signatures and do not
  authenticate the publisher; verify downloads come from this repository's Releases page.
- Device tooling (`tools/device.sh`) never flashes firmware.
