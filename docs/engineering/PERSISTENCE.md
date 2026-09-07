# Commands, saves, and durable progression

This specification deliberately replaces the original two-file save suggestion.

## One durable profile

Use a single `profile.json` containing meta progress, settings, and the active run (or null between runs). Keep `profile.backup.json` as the previous validated revision. Independent run/meta commits can lose or duplicate prestige, so they are prohibited.

Default macOS directory: `~/Library/Application Support/AntFarmSim/`. Plan Windows under the user's local app-data directory and Linux under XDG data home (fallback `~/.local/share/AntFarmSim/`). Resolve paths in the platform adapter; simulation never reads HOME. Tests and headless runs use an explicit `--save-dir` temporary directory.

Enforce one writer per profile using a platform lock owned for app lifetime. If already locked, offer read-only viewing or a different profile location; do not pretend two processes can safely alternate atomic replacements.

## Profile envelope (proposed schema v1)

```text
schema_version: 1
revision: uint64, increasing for each committed write
content_version: string
profile_id: generated stable identifier
phase: ActiveRun | BetweenRuns
meta:
  legacy_wallet: int64 >= 0
  legacy_earned_total: int64 >= 0
  legacy_spent_total: int64 >= 0
  vigor_tier: integer 0..4
  industry_tier: integer 0..4
  generation: uint64
  successful_flights: uint64
last_flight_receipt: null | { run_id, earned_legacy, births, winged_queens }
settings: { ui_scale, reduced_motion, preferred_speed }
run: null | RunSnapshot
```

RunSnapshot includes run ID, seed, tick, RNG states, immutable base content snapshot/hash, owned traits applied at founding, run-upgrade levels, focus/cooldown, assisted flag, phase/maturity latch, stats and Work/remainder, initial nest area, terrain, pheromones, sources/refill remainder, entities with stable IDs, paths/commitments/cargo/reservations, egg-assignment counter, brood progress/care, feeding remainders, aging/death timers, and next entity ID.

Serialize component fields by stable names in ID order, enum strings or stable documented values, and explicit units. Never persist EnTT handles, pointers, raw struct bytes, wall-clock-derived biological age, or OS-specific paths in a run.

For deterministic continuity preserve active paths and reservations in the snapshot. Rebuild BFS fields and spatial buckets on load; revalidate each saved path before use.

Navigation revisions are rebuilt from scratch on load, so their absolute values cannot be compared across a save. Store *whether* a cached path and the frontier set were current for the live topology (`path_valid`, `frontiers_valid`) and restore that fact, marking anything stale with revision 0, which the grid never issues. Restoring a stale path as current skips a replan the live colony still owed itself, and the run silently diverges. If a valid old save cannot preserve behavioral continuity after a migration, say so in migration notes. Do not silently drop state and claim hash equivalence.

## Validation and migration

Read into a DTO without mutating the live session. Validate file size <=64 MiB; integer overflow/range; supported schema; world dimensions bounded by 512x512; entity limits in SIMULATION; unique IDs; existing reference targets; finite/bounded coordinates; valid cell materials; nonnegative resources; stage/role compatibility; legal upgrade levels; wallet = earned - spent; phase/run consistency; parameter bounds; reservation amounts <= source quantities; cargo and assignment integrity; expected array sizes.

Derived population/capacity values are recomputed and checked, not trusted. Preserve food sink/source counters for ledger validation. Reject malformed snapshots with an actionable message. Do not “repair” an arbitrary corrupted economy by clamping a negative wallet to zero.

A newer unsupported schema opens a recovery screen without overwriting the file. Older schemas migrate one version at a time in memory with fixture tests before committing. At v1 there is no invented v0 migration. A content mismatch loads the embedded balance snapshot for the active run; new runs use current validated content.

## Save protocol

All saves occur at a completed tick boundary with no outstanding mutation queue. Autosave every 30 real seconds while active, plus orderly exit, menu save, and durable transitions. Real time only schedules I/O; it never grants progress.

1. Create an owning candidate snapshot and assign next profile revision.
2. Encode; validate the encoded candidate using the same decoder before touching the current file.
3. Write a unique temporary sibling file. Flush file contents using the platform durability API.
4. Preserve the currently validated profile as backup through its own temporary replacement. If backup creation fails, keep the current profile and report failure.
5. Atomically replace profile with the temporary candidate on the same filesystem using platform-appropriate replacement (POSIX rename / Windows replacement API).
6. Sync directory metadata where supported. Report whether replacement committed even if the later durability sync fails; reload the authoritative file before accepting a retry in an uncertain outcome.
7. Only after the committed state is known, swap candidate into the live session and show the success transition.

The previous file remains available if interrupted before replacement. After replacement the new revision is authoritative even if animation never ran. Do not delete current save before renaming the candidate. Power-loss guarantees depend on platform/filesystem; describe them precisely after implementing and testing the adapter.

Use a small `FileOps` adapter for injected write/flush/backup/replace failures. No asynchronous background writer is needed at this scale. If synchronous writes cause measured frame hitches, move only immutable snapshot encoding/writing later, preserving serialized commit ordering.

## Prestige transaction

The flight panel pauses the app and shows a payout from current state. `PrepareFlight(expected_run_id, expected_revision)` revalidates queen, maturity, winged-queen count, assisted status, and active phase. The candidate increments wallet/earned, stores receipt, increments successful flights, and sets run=null/BetweenRuns. Persist this one candidate. On failure keep the live colony and preview. On success accept candidate and animate/open shop.

Legacy purchase similarly validates next tier, shared wallet, and cost in a candidate; update wallet/spent/tier and commit together. StartRun creates a complete new snapshot with a fresh run ID, validates it, persists, then enters the world. AbandonRun requires player confirmation and commits BetweenRuns without reward.

Only one durable action can be in flight. A second click is rejected/busy. A stale command cannot apply to a different run. On restart, phase and receipt determine what is already done; no payout logic runs merely because a saved run looks mature.

## Recovery UX and coverage

On load failure try the backup only after verifying it; tell the user it is an earlier revision and request the recovery choice before replacing current data. A failed current load must not trigger an autosave over the original. If both are invalid, allow a fresh profile at a new location while retaining files for diagnosis.

Implemented in T010 as `SaveService::replace_unreadable`, the one entry point that moves an unreadable `profile.json` to `profile.corrupt.N.json` before committing a replacement. It is never called automatically: both the backup restore and the start-over path run only from an explicit player choice in the paused recovery prompt, and no autosave is scheduled while that prompt is open.

No offline progress in v0.1. Load paused at the exact saved tick, with a brief “Colony resumed” message. Test uninterrupted vs. save/load continuation with active cargo, pending paths, near-maturation brood, and a prestige-ready colony. Tests must inject failures at every save stage and verify no duplicate Legacy, lost accepted tier purchase, or overwritten corrupt original.

## Additive world fields (T014e)

Schema 2 gained three optional world fields rather than a new version, because every one of them is
recoverable from a file that lacks it:

- `granary` — the food piles. A profile written before food had a place in the nest restores as
  totals with no heaps, and the world lays them out on load from the stored totals, so the colony
  continues with the grain it had and nothing is created or duplicated. A profile that carries
  heaps must have them add up to its stores, or it is rejected.
- `brood[].carried_by` — the nurse holding a brood item, defaulting to none.
- `actors[].forager.store_cell` — the heap a load is being carried to, defaulting to the origin,
  which simply makes the forager pick a heap again on its next tick.

`Material` gained `Stone` and `ForageState` gained `Storing`, both appended, so existing values keep
their meaning. A file written by this version is not readable by an older binary; that direction was
never promised.

## Schema 3 — rooms and finite food (T014f)

Schema 3 could not be an additive change: forage sites became a variable-length list with an end,
foragers name a site by its stable id rather than a slot, and the wandering dig faces were replaced
by the room list that is now the colony's whole reason to excavate.

- `sources` is a list of at most `kMaxFoodSources`, each with `known` and `appeared` and no refill.
- `rooms` replaces `dig_faces`: centre, radius, kind and whether it is finished.
- `rng` gained a `world` stream, which decides where and when sites appear, so a colony that
  behaves differently still meets the same food.
- `next_source_spawn`, `recruiting_source` and `recruit_until` carry the appearance schedule and
  the live recruitment alert.
- `actors[].forager` carries `source_id` and `scout_target` in place of `source_index`.
- `frontiers_valid` is gone; the connectivity survey is derived from the grid on load.

A schema 2 profile migrates in memory: its two sites become finite and already known, its foragers'
slot numbers are mapped to the ids of those sites, the faces are dropped, and the colony is given
the founding room set at its usual offsets. Where the old nest already has the space the rooms open
finished; where it does not, the colony digs them out, which is the same work it would do for any
room it decided it needed. Migration cannot invent an appearance schedule the old format never had,
so the restored colony continues at the same tick with the same ground, ants, brood and food, but
not with a bit-identical canonical hash; the migration test asserts exactly that.
