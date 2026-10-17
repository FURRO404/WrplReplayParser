# Lux's PR review: findings and investigation notes

Review target: [LivingTheDagor/WrplReplayParser PR #10](https://github.com/LivingTheDagor/WrplReplayParser/pull/10), Toothless fork branch `toothless-slop/develop`, commit `e96ae42e65b42bb7ff2ff07994a6a3897ae2e81d` at the time of investigation. Line references below refer to this checkout and may move as the code changes.

Lux considers all nine items merge blockers. The investigation below records the original behavior at the review commit. The implementation has since been updated as described in the status list below. The original runtime investigation used a copy of the source under `/tmp/wrpl-stream-review`; temporary files may not persist.

## Implementation status

1. Shared archetype read scopes now reuse the outer lock for nested reads on the same thread, while retaining the lock against writers until the outer scope exits.
2. Ground sync events carry a parser-state identity; `ground_weapons` rejects foreign events and checks weapon and ammunition ranges before indexing.
3. Weapon seeker lengths are checked before any field extraction, with an overflow-safe backing-buffer length check.
4. The 518-bit aircraft block's slot presence flags determine the state offset; a block with no present slot is left undecoded. For the reproduced third-slot sample, shifting the original field offsets by one bit yielded direction `(-0.45364, 0.00875, 0.89114)`, with magnitude approximately 1.
5. RPM reads the nested stream's compressed bit length and then its complete 16-bit values.
6. Cockpit reads the nested stream before its parameter count and checks that the declared entries fit. The shared nested-stream reader also checks the aligned byte range and compressed length without overflowing its bounds check.
7. Store velocity uses the signed 16-bit scale `2000 / 32767`.
8. Unknown fields now use documented `unknown_field_*` names, with explicit presence and encoding information in C++ and Python. This changes the PR's proposed Python API: sensor `f2`, `f3`, `f147`, `b6`, `i149`, and `i150` become `unknown_field_2`, `unknown_field_3`, `unknown_field_147`, `unknown_field_6_data`, `unknown_field_149`, and `unknown_field_150`; designation `v1`–`v15` use their `unknown_field_*` identifiers except `v3`, which becomes `has_unknown_field_4`; `compressed` becomes `uses_compressed_vector`; seeker `head_b` becomes `unknown_header_bit`. Fields that were not present now return `None` where applicable.
9. The distance maximum has been removed. `ready` documents the raw value, the absent-record case, and the unresolved meaning of a present zero.

The additional aircraft sync failure described at the end remains outside Lux's nine-item review and has not been changed by these fixes.

### Unknown-field representations

These identifiers apply to the C++ state fields and Python properties. Meanings, units and reference frames are unknown unless stated otherwise. `None` below refers to the Python property when the value was absent.

| Sensor field | Representation and presence |
| --- | --- |
| `unknown_field_2` | Decoded float32: normalized signed 16-bit value for kind 1 with `has_state`, or a sent float32 for kind 2 when on and kind 4 when its state is present. Otherwise `None`. |
| `unknown_field_3` | Sent float32 for kind 2 when on and kind 4 when its state is present; otherwise `None`. |
| `unknown_field_147` | Sent float32 for kind 2 when on; otherwise `None`. |
| `unknown_field_6_data` | Four raw bytes for kind 2 when on. For kind 1 with state, byte 0 is an optional raw 8-bit value and byte 1 holds an optional decoded 1-bit value; the remaining bytes are zero. The respective presence flags are `has_unknown_field_6_byte0` and `has_unknown_fields_149_150`. `None` when no values were sent. |
| `unknown_field_149`, `unknown_field_150` | Decoded signed 2-bit values for kind 1 with state and `has_unknown_fields_149_150`; otherwise `None`. |

| Designation field | Representation and presence |
| --- | --- |
| `unknown_field_1`, `unknown_field_2` | Unsigned 8-bit values, always present. |
| `has_unknown_field_4` | One-bit presence flag, originally field 3. |
| `unknown_field_4` | Three float32 components if `has_unknown_field_4`; otherwise `None`. |
| `unknown_field_5` | Float32, always present. |
| `unknown_field_6`, `unknown_field_7` | Three float32 components; field 6 is present when `uses_compressed_vector` is true, field 7 when it is false. The other property is `None`. |
| `unknown_field_8`, `unknown_field_10`, `unknown_field_12`, `unknown_field_13` | One-bit values, always present. |
| `unknown_field_9` | Three decoded float32 components, always present. With `uses_compressed_vector`, the wire representation is three signed 16-bit components scaled to 4000; otherwise it is three float32 values. |
| `unknown_field_11` | Optional unsigned 8-bit value normalized to float32 in `[0, 1]`; `None` when absent. |
| `unknown_field_14`, `unknown_field_15` | Optional unsigned 8-bit and 32-bit values respectively; `None` when absent. Some field 15 values identify a target, exposed separately by `target_uid`. |

`SeekerEvent.unknown_header_bit` is an unknown one-bit field immediately before the weapon seeker block. It is only present for the weapon source; Python returns `None` for other sources.

## The nested stream format

Dagor's `BitStream::Read(BitStream&)` reads a **compressed bit count**, aligns to the next byte, then reads that many bits into a nested stream. See [the parser's Dagor BitStream implementation](3rdParty/Dagor_Engine/include/danet/BitStream.h#L312) and [Dagor Engine's implementation](https://github.com/GaijinEntertainment/DagorEngine/blob/75723669297e48e200a0dc67b18c1629e0975daf/prog/dagorInclude/daNet/bitStream.h#L279). A fixed number of prefix bytes is not valid for every length. The original PR handlers for RPM and cockpit data bypassed this nested stream reader and assumed a particular prefix shape.

## 1. P1 — Nested ECS queries can deadlock

- **Code:** [event dispatch and query implementations](replay/ecs/EntityManagerQuery.cpp#L874), especially `sendEventImmediate`, `broadcastEventImmediate`, `performQueryStoppable` and `performQuery`; [archetype creation](replay/include/ecs/EntityManager.h#L153); [example query](replay/events/BaseEntityES.cpp.inl#L124).
- **Finding:** The event dispatch methods hold a shared lock on `archetypes_mtx` while invoking handlers. The query methods also acquire that shared lock. A handler performing a nested query can therefore attempt to reacquire the same mutex while a writer waits for it. Lux reports that the nested reader can block behind the writer, while the writer waits for the outer reader, deadlocking concurrent parses.
- **Evidence:** The lock scopes and nested acquisition are visible in the code. The specific deadlock has not been reproduced in this investigation; its exact behavior depends on the mutex implementation and scheduling.
- **Follow-up:** Design locking around handler execution and nested queries so the same thread does not recursively acquire this mutex, while preserving protection against concurrent archetype and query updates. Verify with a controlled parallel parsing/deadlock scenario.

## 2. P2 — `ground_weapons(event)` accepts offsets from another state

- **Code:** [Python state binding](python/state/State.cpp#L160).
- **Finding:** The accessor uses `event.weapons_begin` and each weapon's `rounds_begin` to index the receiving state's `GroundWeapons` and `GroundRounds`. It does not check that the event belongs to that state or that the ranges fit its pools. An event from another replay can produce an out-of-bounds access or silently return the receiving replay's data.
- **Evidence:** The unchecked indexing and iterator construction are directly visible. A cross-state call has not been reproduced here.
- **Follow-up:** Establish event ownership and validate both pool ranges, including overflow-safe bounds checks, before accessing them. Reject foreign events even when their offsets happen to be in range.

## 3. P2 — Short weapon seeker blocks are read before validation

- **Code:** [seeker block reader and decoder](replay/mpi/PositionSync.cpp#L160), especially `SeekerByte` and `DecodeSeeker`; [Python seeker properties](python/mpi/battle_messages.cpp#L376).
- **Finding:** The decoder calls `SeekerByte(ev, 8)` before confirming that a weapon block has a supported length. The existing check only establishes that the buffer holds the declared number of bits; it does not establish that bits 8–15 exist. Lux identifies 0-, 1-, 7- and 8-bit blocks as out-of-bounds cases, with invalid shifts also possible on other short blocks. Python properties invoke this decoder and can expose the crash.
- **Evidence:** The access before the supported-length checks is directly visible. This investigation identified the same short-block issue from code review; it has not run a sanitizer reproduction.
- **Follow-up:** Validate the supported source and exact block format before reading fields, and use checked bit extraction so malformed or truncated blocks cannot access beyond their declared bit length.

## 4. P2 — Aircraft seeker offsets depend on the slot header

- **Code:** [518-bit aircraft seeker decoding](replay/mpi/PositionSync.cpp#L198).
- **Finding reported by Lux:** The block has presence flags for multiple slots and a weapon ID before each present seeker state. The start of the state varies by slot. The hard-coded offsets match the second slot; first- and third-slot states require offsets one bit earlier and later respectively. This affects `seeker_dir`, `ir_state`, `fov_half` and other fields. Lux observed vector magnitudes around `1e19` on Su-24M, A-4N and F-111F records.
- **Independent reproduction:** In server replay `path_engine_wt/replays_test/server/72e390f0002365ae`, an `a_4e_early_iaf` 518-bit aircraft seeker at 363.595 s decoded to approximately `(3.51828e19, -7.15454e-21, 7.56249e-20)`, with magnitude `3.51828e19`. The API documents this as a unit vector. The fixed offsets are present in the code; this reproduces the invalid result, but does not by itself establish the slot layout or verify which offset adjustment applies. Lux's specific other aircraft examples have not been independently checked here.
- **Follow-up:** Parse the presence flags and IDs in order, derive each present state's offset, and decode fields relative to it. Check all slot positions against captured records and verify documented unit-vector behavior where applicable.

## 5. P2 — RPM mistakes the nested stream length prefix for a count

- **Code:** [RPM payload reader](replay/mpi/ObjectDispatcher.cpp#L394).
- **Finding:** The handler reads one byte and uses `head >> 4` as the number of 16-bit values. That happens to match the compressed bit length for up to seven values. At eight values the prefix expands, so continuation bytes are consumed as RPM data. Lux reports a ten-value Me 264 record exposing 44545 and 44556 where the actual RPM is 3246.
- **Independent reproduction:** An isolated C++ diagnostic wrote nested streams with the actual `BitStream::Write(child)` and passed them to the PR's `RpmMessage::readPayload`. Seven values `3200..3206` decoded correctly. Eight values `3200..3207` decoded as `32769, 33036, 33292, 33548, 33804, 34060, 34316, 34572`; ten values were likewise shifted. The eight- and ten-value streams had two-byte prefixes. This confirms the failure mechanism without depending on a particular replay.
- **Follow-up:** Read the nested stream using the bitstream API, validate its length and decode the 16-bit values from the nested stream. Include boundary cases at 7 and 8 values and malformed or truncated payloads.

## 6. P2 — Cockpit assumes a two-byte nested stream prefix

- **Code:** [cockpit payload reader](replay/mpi/ObjectDispatcher.cpp#L378).
- **Finding:** The reader consumes a fixed `uint16_t` before reading the parameter count. The prefix is actually a compressed nested-stream bit length. For this payload it occupies one byte for 0–2 parameters, two bytes for 3–340, and three bytes starting at 341. Lux notes that some short valid messages can be accepted with an empty list, silently losing values.
- **Independent reproduction:** A diagnostic encoded a count and `(uint16_t id, float value)` pairs using `BitStream::Write(child)`, then invoked the PR's reader. Counts 0, 1 and 2 were rejected, counts 3 and 340 were accepted with the expected counts, and count 341 was rejected. The rejected messages may leave partial data in the diagnostic's message object; a rejected message is not dispatched. This reproduces the prefix-boundary failure, though not the separate accepted-empty example.
- **Follow-up:** Read the child bitstream with the actual API and validate the count and all pairs within that child. Exercise counts 0, 1, 2, 3, 340 and 341 as well as truncated input.

## 7. P2 — Store velocity conversion

- **Code:** [store sync velocity](replay/mpi/PositionSync.cpp#L1026).
- **Finding reported by Lux:** Each signed 16-bit component is currently divided by 16; the correct conversion is `raw * 2000 / 32767`. For nonzero values the current magnitude is too high by about 2.396875%. At raw 16000, it returns 1000 m/s rather than approximately 976.5923 m/s.
- **Evidence:** The current `/ 16.f` conversion is directly visible. The correct protocol scale has not independently been established from a source or a captured record in this investigation.
- **Follow-up:** Confirm the scale against the serializer or reliable captured values, correct the conversion, and check negative values and endpoints.

## 8. P3 — Public field names do not explain the data

- **Code:** [Python sensor and designation properties](python/mpi/battle_messages.cpp#L206), [seeker event](python/mpi/battle_messages.cpp#L376), and [C++ sensor types](replay/include/mpi/SensorStates.h).
- **Finding:** Public names include sensor `f2`, `f3`, `f147`, `b6`, `i149`, `i150`; designation `v1` through `v15`; and seeker `head_b`. These do not communicate representation, meaning or presence conditions.
- **Follow-up required by Lux:** Use descriptive names only where meaning is established. Identify unknown values as raw or unknown and document their type, width, stable field identifier and presence conditions. Document units and reference frames where applicable. Keep C++ names, Python properties and documentation aligned; do not invent semantic names for unknowns. Account explicitly for any public API compatibility implications.

## 9. P3 — Ground-sync documentation makes invalid guarantees

- **Code:** [weapon `ready` property](python/mpi/battle_messages.cpp#L326), [ground `aim_distance` property](python/mpi/battle_messages.cpp#L365), [C++ weapon accessors](replay/include/mpi/SensorStates.h#L242).
- **Finding reported by Lux:** The `aim_distance` documentation claims a 10 km maximum, but a returned value of `19899.296875` m contradicts it. The `ready` documentation claims an absolute battle time equal to last shot plus reload duration, but a present ammunition record can yield `ready == 0.0` after positive `last_shot` (example `113.58333587646484`). The accessor returns `0.0`, not `None`, in that case.
- **Evidence:** Both current documentation claims and the accessor behavior are visible in code. The cited replay values have not been independently located in this investigation.
- **Follow-up:** Remove the unsupported distance maximum; do not use it for validation or clamping. Describe the actual meaning and validity conditions of `ready`, including the present-but-zero case, based on evidence rather than assuming zero is a usable timestamp.

## Additional finding outside Lux's list: aircraft stream failure is ignored

In [aircraft sync parsing](replay/mpi/PositionSync.cpp#L252), `FMSync` can return false when it cannot resolve a unit. [The packet handler](replay/state/ParserState.cpp#L199) calls it without checking the return value. In an instrumented copy, the server replay `WrplDecoder/replays/server/127b004900163e25` encountered such a failure at 84.514 s with UID 400 missing and 15,546 bits unread in the 73,520-bit packet. Other failures appeared in the scanned collection.

This shows a failed stream parse being ignored; it does **not** establish why that UID was missing or whether earlier bits were misread. Determine the intended handling of missing units and propagate or report parsing failures without assuming that all remaining bits are valid.

## Scope of the investigation

The diagnostic used the actual nested-stream writer and original PR readers in an isolated source copy. It also inspected 126 locally available server/client replay paths with additional stream instrumentation; the RPM and cockpit prefix edge cases were not observed in that collection. Absence from this sample does not disprove Lux's examples or establish general correctness. The concurrency, foreign-event, all seeker-slot layouts, velocity-scale and specific ground-sync examples still need the focused checks noted above.
