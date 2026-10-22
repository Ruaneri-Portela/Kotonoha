# KTRF Semantic Freeze Gate v0.1 — School Days HQ

Status: **Pre-freeze gate**

This document defines the final semantic certification step before KTRF work moves from Canonical Routing IR semantics into the physical `.ktnroute` binary layout.

The freeze target is:

```text
KTRF semantic model              v0.1.0
School Days HQ profile           v1.0.0
School Days executable oracle    school-days-routing-oracle-v1
oracle commit                    614461c2b14951ba117b9d2dedb4983cfa8ae8e6
```

The freeze does **not** define binary bytes. It certifies the semantic contract that the future binary must serialize without changing.

## 1. Why freeze semantics before bytes

KTRF intentionally separates:

```text
semantic model
    ↓
Canonical Routing IR
    ↓
future binary serialization
```

The binary container is therefore not allowed to invent, remove or reinterpret routing behavior.

A semantic freeze gives the future `.ktnroute` compiler/loader a stable reference target. A binary round trip will be considered conformant only if it decodes back to the same frozen semantics and passes the same oracle differentials.

## 2. Required gates

The freeze runner executes the complete semantic stack again from a fresh School Days export.

Required gates are:

```text
1. generic JSON Schema + semantic validation
2. deterministic School Days oracle -> Canonical Routing IR export
3. School Days profile conformance validation
4. 22-ending causal C++/KTRF differential
5. targeted Transition closure
6. exhaustive Choice / Feeling differential
7. source Effect stress differential
8. broader finite boundary state matrix
9. deterministic freeze manifest generation
```

No previous console PASS is accepted as a substitute for this final rerun.

## 3. Frozen conformance expectations

The semantic freeze gate currently requires:

```text
Nodes                              1857 / 1857
Executable Transitions             2458 / 2458
Choices                             287 / 287
Choice outcomes                     772
FeelingResolution                   772 / 772
FeelingDelta                        302 / 302
Source Effects                     6183 / 6183

22-ending witness steps            4673
witness Transition coverage        1081 / 2458
targeted Transition fixtures       1377 / 1377
combined Transition coverage       2458 / 2458

Choice scenarios                   1544
Effect-stress scenarios            4916
State-matrix scenarios             3371
State-matrix pending Choice checks  287 / 287

total recorded divergences            0
```

The default state-matrix freeze policy uses:

```text
max pairwise boundary candidates per source Node = 32
```

Changing that policy is additional evidence; it does not silently redefine the frozen default gate.

## 4. Deferred Choice invariant discovered by the broader matrix

The broader state matrix found and closed an important semantic gap before freeze.

For a Node that owns an uncommitted `ktrf:deferred` Choice:

```text
choice_result = pending
      ↓
host asks for ktrf:next
      ↓
routing evaluation is blocked
      ↓
result = unresolved
```

This applies even if an outgoing Transition is otherwise unconditional.

Once a valid Choice result is committed, the Choice outcome Effects run and a later routing trigger may evaluate the outgoing Transitions normally.

Direct state-injection test harnesses restore committed-Choice bookkeeping explicitly when they inject a post-Choice snapshot. Restoration does not replay Choice Effects.

This rule is now part of the normative KTRF v0.1 semantics rather than School Days-only test behavior.

## 5. Freeze manifest

Tool:

```text
tools/ktrf/freeze_sdhq_semantic_v0_1.py
```

The tool produces:

```text
build/ktrf/school-days-hq.semantic-freeze-v0.1.json
build/ktrf/school-days-hq.semantic-freeze-v0.1.sha256
```

The manifest contains SHA-256 records grouped into:

```text
semantic_contract
school_days_profile_mapping
frozen_oracle
conformance_toolchain
```

It also hashes:

- the generated canonical School Days IR;
- all required JSON conformance reports;
- the grouped semantic/profile/oracle/toolchain inputs;
- a final semantic-freeze root digest.

No wall-clock timestamp is stored, so rerunning the same committed inputs and deterministic conformance reports produces the same manifest bytes.

## 6. Scoped working-tree cleanliness

The user may have unrelated local work in the same repository.

The freeze generator therefore does **not** require the entire Git working tree to be clean. It requires only the files that define or verify this KTRF semantic freeze to have no uncommitted modifications.

Unrelated files such as Android work are outside this scoped check and are not reset, restored or modified by the freeze tooling.

## 7. Run

From the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File `
  .\tools\ktrf\run_sdhq_semantic_freeze.ps1
```

This is intentionally a full rerun and may take longer than the individual gates.

Success ends with:

```text
KTRF SEMANTIC FREEZE MANIFEST PASS

=========================================================
 KTRF / SCHOOL DAYS SEMANTIC FREEZE GATE v0.1: PASS
=========================================================
```

The final output also prints:

```text
ir_sha256=...
semantic_contract_sha256=...
profile_mapping_sha256=...
oracle_inputs_sha256=...
conformance_toolchain_sha256=...
evidence_sha256=...
semantic_freeze_root_sha256=...
manifest_sha256=...
```

These values are the review anchors for certification.

## 8. What is frozen

After the generated manifest is reviewed and certified, changes to any of the following are semantic changes and require a new semantic version or an explicitly justified compatible revision:

- Transition selection and priority rules;
- unresolved behavior;
- deferred Choice commit/blocking rules;
- Expression semantics;
- Effect ordering and operators;
- Variable typing/scoping semantics;
- Ending registration semantics;
- ExternalHook invocation ordering;
- Node/EntryPoint semantics;
- School Days profile lowering from the frozen oracle;
- required feature/namespace interpretation.

Documentation-only wording changes that do not alter the normative contract still require care because the specification files themselves are part of the freeze digest.

## 9. What is not frozen yet

The semantic freeze does **not** choose:

- binary header byte size;
- integer widths for serialized indexes/counts;
- section IDs or physical section order;
- file endianness encoding;
- alignment rules;
- string table physical layout;
- compression;
- checksum/hash algorithms for the binary container;
- offset width;
- binary extension records.

Those decisions begin only after this semantic gate is certified.

## 10. Next phase after certification

Once the semantic freeze is accepted, the next work package is the physical KTRF `.ktnroute` specification.

The first binary-design pass should define, in order:

```text
1. fixed file header
2. section directory contract
3. byte order and integer widths
4. STRS/string identity model
5. typed semantic sections
6. offsets/alignment and bounds rules
7. integrity/hash model
8. deterministic compiler
9. binary loader/dumper
10. IR -> binary -> decoded IR round-trip conformance
```

The future binary implementation must be tested against the frozen semantic manifest rather than becoming a new source of truth.
