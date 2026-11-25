# Security

Report vulnerabilities privately through
[GitHub private vulnerability reporting](https://github.com/korczis/prismatic-flipperzero/security/advisories/new).
Please do not open a public issue for a vulnerability.

Scope and boundaries:

- The FAP is receive-only. v0.0.1 performs no radio transmission, network access or
  storage writes, and it contains no credentials.
- Release `SHA256SUMS` detect accidental corruption. They are not signatures and do not
  authenticate the publisher; verify downloads come from this repository's Releases page.
- Device tooling (`tools/device.sh`) never flashes firmware.
