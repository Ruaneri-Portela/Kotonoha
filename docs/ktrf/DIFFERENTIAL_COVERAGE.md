# KTRF Differential Coverage — School Days HQ

Status: **Coverage-audit implementation v0.1**

The 22-ending differential is a strong semantic test, but it is not automatically equivalent to covering all 2,458 executable-oracle Transitions.

This document defines how witness coverage is measured before broader state-injection and exhaustive differential work begins.

## 1. Tool

Coverage auditor:

```text
tools/ktrf/audit_sdhq_differential_coverage.py
```

Default report:

```text
build/ktrf/school-days-hq.differential-coverage.json
```

The normal differential runner now executes the coverage audit after the 22-ending zero-divergence test.

## 2. Why coverage is measured separately from PASS/FAIL

A differential PASS means every state comparison actually executed by the corpus matched between:

```text
frozen C++ SchoolDaysRouter
        ==
KTRF Canonical Routing IR interpreter
```

It does **not** mean every Transition, Node or Choice in the oracle was necessarily exercised.

Therefore the project distinguishes:

```text
semantic correctness of executed cases
                versus
structural coverage of the executable oracle
```

Both matter.

## 3. Measured dimensions

The report measures:

- total witness count;
- total executed witness steps;
- unique source Transition IDs exercised;
- unique source Nodes exercised;
- Choice Nodes exercised with an explicit choice/timeout result;
- terminal Transitions exercised;
- non-terminal `callback_38` handoffs exercised;
- terminal `callback_38` handoffs exercised;
- Ending-registration Transitions exercised;
- routing-only logical Nodes exercised.

It also records explicit gap lists for the same categories.

## 4. Frozen denominators

For School Days HQ executable-oracle v1 the important denominators are:

```text
Nodes                         1857
Transitions                   2458
Choice Nodes                   287
Terminal Transitions            23
callback_38 non-terminal         47
callback_38 terminal             23
Routing-only Nodes                2
```

The auditor refuses to run silently against an IR whose frozen inventories do not match those oracle values.

## 5. Coverage report is diagnostic, not a 100% gate yet

The 22-ending corpus was designed first to prove causal reachability of all 22 native endings and deterministic replay of those paths.

It was not designed to touch every executable Transition.

For that reason:

```text
COVERAGE AUDIT PASS
```

means the coverage accounting itself is internally consistent.

It does **not** mean transition coverage reached 100%.

The report exists specifically to quantify the remaining gap.

## 6. Next conformance layer

After measuring the witness baseline, the next implementation stage is targeted state-injection differential testing.

The goal is to construct legal or deliberately controlled pre-routing states that exercise Transition branches not reached by the 22 ending witnesses while comparing the exact same observable state in both runtimes.

That work must preserve the distinction between:

- states causally reachable from Normal New Game;
- synthetic but semantically valid test states;
- invalid/impossible state combinations.

Those categories must never be merged into a single unlabeled coverage number.

## 7. Acceptance direction

Before KTRF binary design begins, the School Days profile should have at least two independent differential layers:

1. **causal witness differential** — 22 complete ending paths from reset;
2. **broad branch/state differential** — targeted or exhaustive coverage of the remaining executable routing decision space.

Binary serialization begins only after the semantic model has survived both layers with zero divergence.
