# Modular code and safe refactoring policy

**New code in this repository must be modular by default. Existing code must move toward modular architecture incrementally, with no intentional change to established functionality.** This applies to human and automated contributors.

## Engineering rules

1. Before editing, examine active branches/PRs, specifications, tests, build scripts, module dependencies and any existing task ownership. Preserve ongoing work and avoid competing edits.
2. Give each module a cohesive responsibility, small documented contracts and explicit ownership of state/resources. Favor dependency injection and composition, avoid hidden global state, tightly coupled UI/engine logic and circular dependencies. Do not make needless micro-modules merely to increase file count.
3. Separate core algorithms, DSP, state and models from GUI, framework, OS, hardware, networking, serialization and persistence adapters. Share domain logic through narrow contracts.
4. Preserve public APIs, exports, parameter IDs, preset formats, plugin identity, automation, CLI behavior, settings and persistent data. An incompatible change requires a separate specified, versioned migration and regression tests.
5. **Refactor in a separate small PR from feature development.** Characterize current behavior before moving code, then extract and delegate via an adapter/facade, keeping the previous entry point until callers migrate. Tests should validate behavior, not merely compiler success.
6. Preserve concurrency, lifecycle, cancellation, error reporting and resource limits. For real-time audio/MIDI callbacks, allow no blocking, dynamic allocation, file/network IO, logging, GUI access or unsafe locks; maintain deterministic bounded execution and validate DSP output.
7. Run focused regression tests and the full applicable supported-platform build/test matrix. Do not lower assertions or skip failing builds to claim success. Report unavailable device, DAW-host or Windows checks as unverified.
8. Document significant modules in the project's existing module index (or add a concise index), recording contracts, dependencies, invariants, error cases and migration notes. Choose the smallest safe next seam; if coverage is inadequate, write tests/specs first instead of changing risky code.
9. Merge only passing reviewed work; use short-lived branches and preserve others' work. Do not force-push or silently alter approved methods.

**Modular design applies to all future work immediately. Converting legacy code is progressive, test-backed and reversible.**
