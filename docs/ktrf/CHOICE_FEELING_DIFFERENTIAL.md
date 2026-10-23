# KTRF Choice + Feeling Differential — School Days HQ

Status: **Implementation draft v0.1**

This layer validates the deferred Choice semantics that are deliberately separate from transition selection.

The transition differential already reached:

```text
combined_transition_coverage = 2458 / 2458
unresolved                    = 0
divergences                   = 0
```

That result does **not** by itself prove every Choice commit or FeelingResolution, because the targeted transition layer injects the post-choice routing state directly.

This document defines the independent Choice/Feeling differential required before binary KTRF serialization begins.

## 1. Oracle boundary

Frozen oracle:

```text
school-days-routing-oracle-v1
614461c2b14951ba117b9d2dedb4983cfa8ae8e6
```

The executable oracle contains:

```text
Choice Nodes          287
FeelingResolution     772
FeelingDelta          302
```

The KTRF exporter maps every source FeelingResolution to exactly one exported Choice outcome and every source FeelingDelta to a `ktrf:add` Effect.

## 2. Deferred Choice semantics under test

The required ordering is:

```text
Choice input
    ↓
write choice_result
    ↓
apply the selected FeelingResolution deltas in order
    ↓
mark that Choice commit as applied/idempotent
    ↓
remain on the same Node
    ↓
NO routing yet
```

A later `ktrf:next` trigger performs transition selection.

This layer therefore does not call `ResolveNext()` as part of the initial Choice commit comparison.

## 3. Exhaustive exported outcomes

`tools/ktrf/diff_sdhq_choices.py` enumerates every exported Choice:

- every valid option from the original choice mask;
- the timeout outcome (`-1`);
- every `source_feeling_resolution_index`;
- every referenced `source_feeling_delta_index`.

The run is rejected unless the IR exposes exactly:

```text
287 / 287 Choices
772 / 772 FeelingResolution rows
302 / 302 FeelingDelta rows
```

Choice outcomes without a FeelingResolution are still tested. They must commit the result and remain on the same Node without synthesizing deltas.

## 4. Two value modes

Every exported outcome is tested twice.

### Baseline mode

All Feeling variables touched by the outcome start at zero.

This proves the direct recovered delta result.

### Stress mode

Touched Feeling variables receive deterministic non-zero/negative seeds before commit.

This proves that the generic `ktrf:add` lowering preserves additive semantics rather than accidentally behaving like assignment.

## 5. C++ oracle protocol

`SchoolDaysOracleTrace` adds a machine-readable test command:

```text
ACCEPT <choice-result>
```

The command calls the existing frozen:

```cpp
SchoolDaysRouter::AcceptChoice(result)
```

and emits the post-commit state without routing.

The trace adapter also retains:

```text
RESET
INJECT
SESSION
GLOBAL
RESOLVE
STEP
```

The mutable injection path exists only under `KOTONOHA_ROUTER_TEST_ACCESS`; production builds do not receive that definition.

## 6. Per-scenario checks

For each outcome and each seed mode, the differential performs three operations.

### Initial commit

The selected valid result must:

- return accepted;
- set `choice_result` to the selected value;
- set the C++ `feelingApplied` guard;
- apply all expected deltas in source order;
- leave Route/Scene unchanged;
- leave the current SceneKey unchanged;
- leave globals/endings unchanged.

### Same-result repeat

Committing the same result again must:

- return accepted;
- apply no delta twice;
- leave the complete observable state unchanged.

This proves idempotence.

### Different-result repeat

After a Choice has already been committed, attempting another valid outcome must:

- be rejected;
- apply no effects;
- leave the complete observable state unchanged.

This matches the frozen C++ rule:

```text
if choice_result != pending:
    return choice_result == requested_result
```

## 7. State comparison

The C++ and KTRF runtimes are compared for:

- accepted/rejected result;
- current SceneKey;
- Route/Scene coordinate;
- `choice_result`;
- `callback34`;
- every recovered session Variable;
- every recovered global Variable;
- Ending registration list;
- no movement to another Node during Choice commit.

The C++-only `feelingApplied` boolean is treated as the implementation guard corresponding to the KTRF interpreter's committed-Choice state. KTRF does not expose it as a portable core Variable.

## 8. Run

From repository root:

```powershell
powershell -ExecutionPolicy Bypass -File `
  .\tools\ktrf\run_sdhq_choice_diff.ps1
```

The runner performs:

```text
1. syntax validation
2. fresh oracle -> canonical IR export and validation
3. build the compiled C++ oracle trace adapter
4. exhaustive Choice + Feeling differential
```

Expected success tail:

```text
feeling_resolutions=772/772
feeling_deltas=302/302
divergences=0
CHOICE + FEELING DIFFERENTIAL PASS

KTRF / SCHOOL DAYS CHOICE + FEELING CLOSURE: PASS
```

Machine-readable report:

```text
build/ktrf/school-days-hq.choice-diff.json
```

## 9. What a PASS proves

A PASS proves direct C++ versus KTRF execution equivalence for every exported School Days Choice outcome under both zero and non-zero Feeling baselines, including Choice idempotence and post-commit alternate-result rejection.

Together with 100% transition closure, this establishes independent coverage of both sides of the delayed-routing boundary:

```text
Choice commit / Feeling application
            ↓
        timeline
            ↓
      ktrf:next
            ↓
Transition selection / Effects / destination
```

## 10. What remains

Even after Choice + Feeling closure, KTRF binary serialization is still gated on value-sensitive Effect stress and broader state validation.

The next layers are:

1. dedicated source Effect stress, especially `SetSessionFromSession` / `ktrf:copy`, ordered writes and callback/ending ordering;
2. broader reachable/projected-state differential validation where practical;
3. conformance freeze for the semantic IR;
4. only then physical `.ktnroute` binary layout design.

The distinction remains intentional: KTRF must serialize semantics that have already been proven rather than inventing semantics inside the binary codec.
