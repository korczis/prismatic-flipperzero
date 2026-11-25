# Contributing

1. `just doctor`, then `just sdk`.
2. Make the change; `just verify` must pass (it is what CI and the finish contract run).
3. Every changed line in `tools/*.py` must be run by a test (`just test` enforces it).
   Device-only C code is verified with real device frames (`just walk`), never mock-ups.
4. Conventional commits: `type(scope): description`.

The repository is supervised by [Majordomus](https://majordomus.dev): read `AGENTS.md`.
Never hand-edit generated regions or the projections; edit `.ai/repo/policy.yaml` and run
`majordomus update`.
