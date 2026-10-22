# KTRF Source Effect Stress Differential — School Days HQ

Status: **Implementation draft v0.1**

This layer strengthens the already-complete transition differential by rerunning every executable transition that owns at least one recovered source Effect under additional non-default pre-state seeds.

The frozen executable oracle contains:

```text
source Effects  6183
```

Transition closure already proved that every executable transition can be selected and that C++ and KTRF produce the same complete post-state for at least one fixture. Therefore every source Effect attached to those transitions has already executed at least once.

The stress layer asks a stricter question:

> Do data-sensitive Effects still agree when relevant pre-state Variables are deliberately seeded away from their normal defaults, while keeping the same transition selected?

## 1. Components

Stress differential:

```text
tools/ktrf/diff_sdhq_effect_stress.py
```

Runner:

```text
tools/ktrf/run_sdhq_effect_stress.ps1
```

It reuses:

```text
tools/ktrf/diff_sdhq_targeted_transitions.py
tools/ktrf/diff_sdhq_witnesses.py
tools/ktrf/interpreter.py
tests/SchoolDaysOracleTrace.cpp
```

No new routing evaluator is introduced.

## 2. Scope

Recovered source Effect kinds are lowered as follows:

```text
SetSessionConst         -> ktrf:set
SetSessionFromSession   -> ktrf:copy
SetGlobalConst          -> ktrf:set
RegisterEnding          -> ktrf:register-ending
Callback                -> ktrf:call-hook
```

The stress differential enumerates every KTRF Effect carrying `source_effect_index` metadata and requires the inventory to remain exactly 6,183.

Every such Effect must be owned by at least one executable Transition. A source Effect that is not reachable through an executable Transition is treated as an inventory error rather than silently ignored.

## 3. Stress fixture construction

For every Transition that owns at least one recovered source Effect, the existing targeted-transition solver first finds a valid branch-selecting pre-state.

The stress layer then derives two deterministic additional seed modes:

```text
positive
negative
```

Variables touched by source Effects are pre-seeded when it is safe to do so without changing which Transition wins first-match selection.

The seed algorithm is deterministic across Python processes and does not use Python's randomized `hash()`.

## 4. Branch-preservation rule

A Transition is selected only after all earlier-priority branches at the same source Node have failed and the target predicate has passed.

Therefore Variables referenced by any local branch predicate are treated as branch-sensitive and retain the targeted solver's satisfying values.

The following Variables are also protected from arbitrary pre-seeding:

```text
ROUTE
SCENE
choice_result
callback34
```

`ROUTE` and `SCENE` must remain consistent with the logical source Node. `choice_result` and `callback34` participate in School Days routing lifecycle semantics.

Only non-branch, non-coordinate Variables are freely stress-seeded.

After seeding, the selector re-evaluates the source Node. If the selected transition changes, the scenario fails immediately.

## 5. Why two modes

A single non-zero seed can accidentally equal a constant written by the Effect or accidentally hide an ordering dependency.

The positive and negative deterministic modes create two distinct pre-states for every stressable Variable.

For literal `ktrf:set` operations, the generator additionally avoids an accidental pre-state value equal to the value being written whenever the target is free to perturb.

This strengthens overwrite testing.

## 6. Copy sensitivity

`SetSessionFromSession -> ktrf:copy` is the most important transition Effect for value sensitivity because the result depends on another Variable's pre/effect-order state.

The report distinguishes copy sources that are:

```text
free_source_to_preseed
branch_or_coordinate_constrained_source
self_copy
```

A free copy source is exercised under two non-default seed modes.

A branch-constrained source is still executed and compared, but its value cannot be perturbed independently without risking selection of a different Transition. The report keeps that limitation explicit rather than claiming stronger coverage than was executed.

## 7. What is compared

The stress layer delegates each fixture to the same post-state comparator used by targeted transition differential testing.

For every scenario it checks:

- selected source Transition ID;
- Advanced/Terminal result;
- post-step ROUTE/SCENE;
- nonterminal current Node and SceneKey;
- destination SceneKey;
- callback order;
- accumulated Ending registrations;
- newly registered Ending ID;
- `choice_result` reset;
- `callback34`;
- every recovered session Variable;
- every recovered global Variable.

Because the full post-state is compared after ordered Effect execution, write ordering differences are observable.

## 8. Run

From repository root:

```powershell
powershell -ExecutionPolicy Bypass -File `
  .\tools\ktrf\run_sdhq_effect_stress.ps1
```

Success ends with:

```text
SOURCE EFFECT STRESS DIFFERENTIAL PASS

KTRF / SCHOOL DAYS SOURCE EFFECT STRESS: PASS
```

Default report:

```text
build/ktrf/school-days-hq.effect-stress-diff.json
```

The console also prints the exact source Effect counts by recovered kind and the number of constrained/free copy and constant-set operands.

## 9. Acceptance meaning

A PASS means:

1. all 6,183 recovered source Effects are still represented by executable Transitions;
2. every Transition containing source Effects was differentially executed in both deterministic stress modes;
3. every freely stressable touched Variable was moved away from its ordinary default fixture state;
4. the compiled C++ oracle and KTRF IR interpreter produced zero post-state divergence.

It does **not** mean that every branch-sensitive Effect operand has been independently varied across the complete int32 domain.

Any branch-constrained residuals are reported explicitly and can be targeted later if they materially affect confidence.

## 10. Remaining pre-binary work

After source Effect stress, the remaining semantic-hardening work is broader state differential validation. The transition, Choice/Feeling and source-Effect layers answer different questions and should remain separately reportable.

Only after those layers are stable should KTRF move from semantic IR design into physical `.ktnroute` serialization.
