# KTRF Oracle Boundary Clarification — School Days HQ

Status: **Normative for the KTRF School Days profile v0.1**

This note records a distinction discovered during the first real export of the frozen School Days HQ oracle into Canonical Routing IR.

It does **not** modify, retag or rewrite `school-days-routing-oracle-v1`.

## Two related routing datasets existed during research

The School Days routing work produced more than one useful representation while reverse-engineering converged:

1. a broader structural/research topology recovered from original routing logic;
2. the generated executable Normal New Game router that was subsequently frozen and certified as the C++ oracle.

The six well-known causally-dead branch IDs belong to the first category:

```text
t971
t1243
t1248
t1494
t1824
t1825
```

The frozen generated executable table contains **2,458 Transitions** and does not contain those six IDs.

## Why the first exporter failed

The initial KTRF exporter incorrectly combined two valid facts:

- research had identified and documented six dead raw branches;
- the executable oracle had been frozen at 2,458 Transitions.

It then asserted that the six IDs must also be present inside the generated executable table.

That assertion was false, so the exporter correctly stopped with:

```text
dead transition catalog mismatch: []
```

The empty set was evidence that the executable table did not contain those research-only IDs.

## Correct KTRF rule

KTRF executable conformance targets the frozen executable oracle exactly.

Therefore:

- each of the 2,458 executable-oracle Transitions becomes one KTRF Transition;
- the six structural dead-branch IDs are not synthesized;
- their exclusion is retained as provenance metadata;
- validators assert that those IDs remain absent from executable-oracle Transition entities.

Top-level IR metadata uses:

```json
{
  "excluded_structural_dead_transition_ids": [
    971,
    1243,
    1248,
    1494,
    1824,
    1825
  ]
}
```

## Why this matters for future profiles

KTRF must distinguish:

- **semantic executable model** — what the runtime oracle can actually execute;
- **research provenance** — branches, symbols, offsets or hypotheses useful for historical/reverse-engineering context but not part of the executable model.

A generic format becomes unreliable if research-only topology is silently promoted into executable routing semantics.

The same rule will apply to future Shiny Days, Cross Days or other profiles: provenance may be preserved, but executable entities are created only when the profile's chosen conformance oracle actually contains them.

## Compatibility consequence

This clarification does not require a core IR version change.

It changes only the School Days profile's interpretation of provenance metadata and its conformance tests.

The generic concepts remain unchanged:

- Node;
- Transition;
- Expression;
- Effect;
- Variable;
- Choice;
- Ending;
- ExternalHook;
- ResourceLocator;
- EntryPoint.

## Acceptance condition

A School Days KTRF export is correct only when:

```text
IR Transition count = 2458
```

and:

```text
{971,1243,1248,1494,1824,1825}
    ∩ exported source_transition_ids
= ∅
```

while the same six IDs remain available in explicit exclusion/provenance metadata.
