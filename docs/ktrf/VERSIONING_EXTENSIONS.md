# KTRF Versioning, Features, Namespaces and Extensions v0.1

## 1. Independent version axes

KTRF intentionally separates version responsibilities.

### 1.1 Routing IR version

`ir_version` controls JSON structure and core IR semantics.

Example: `0.1.0`.

### 1.2 Future binary version

The future `.ktnroute` binary has its own version.

Binary version MUST NOT be inferred from `ir_version`.

Container representation may change without semantic-model changes, and semantic features may evolve without using the same binary version number.

### 1.3 Profile version

Each game/profile adapter is independently versioned.

Example:

```json
{
  "id": "overflow.school-days-hq",
  "version": "1.0.0"
}
```

### 1.4 Feature version

Capabilities are independently versioned.

```json
{
  "id": "ktrf:expression-core",
  "version": "1.0.0"
}
```

## 2. Semantic Versioning policy

KTRF uses `MAJOR.MINOR.PATCH`.

Before 1.0, drafts may intentionally change.

After 1.0:

- MAJOR — incompatible semantic/structural change;
- MINOR — backward-compatible capability addition;
- PATCH — clarification or bug fix without intentional valid-document meaning change.

## 3. Required features

`features.required` is a hard compatibility contract.

If the consumer lacks a compatible implementation, it MUST reject before routing begins.

This prevents future Shiny Days/Cross Days semantics from being silently degraded by an older runtime.

## 4. Optional features

Optional does not mean semantically disposable.

A feature may be optional only when its specification guarantees that omission does not alter required routing behavior.

Likely optional:

- debug/source maps;
- authoring annotations.

Usually required:

- new predicate semantics;
- new state mutation semantics;
- routing-significant hook contracts.

## 5. Namespaces

`ktrf` is reserved.

Other prefixes are explicit, for example:

- `overflow.sdhq`
- `overflow.shiny`
- `overflow.cross`
- `vendor.project`

Declaration:

```json
{
  "prefix": "overflow.sdhq",
  "uri": "urn:kotonoha:profile:overflow:school-days-hq"
}
```

Namespace URI is an identifier, not necessarily a downloadable resource.

## 6. Qualified names

Operators, scopes, types, hook symbols, locator schemes, triggers and feature IDs use:

`prefix:local-name`

Examples:

- `ktrf:eq`
- `ktrf:set`
- `ktrf:int32`
- `ktrf:session`
- `overflow.sdhq:history`
- `overflow.sdhq:callback_38`

## 7. Extension blocks

Top-level extension shape:

```json
{
  "namespace": "overflow.shiny",
  "type": "some-extension",
  "required_feature": "overflow.shiny:some-extension",
  "data": {}
}
```

`data` is owned by the namespace.

Core validators may preserve unknown optional extension blocks without interpretation.

## 8. Unknown data rules

A conformant reader:

- MUST reject unsupported required features;
- MUST reject unknown core `ktrf:` operators for the declared IR version;
- MAY preserve unknown optional extension blocks;
- MUST NOT reinterpret unknown namespaced semantics as a “similar” core operation;
- SHOULD preserve unknown optional metadata during round-trip.

## 9. Feature locality

Feature requirements SHOULD be narrow.

Bad:

`overflow.shiny:everything`

Better:

- `overflow.shiny:predicate-x`
- `overflow.shiny:hook-y`
- `overflow.shiny:resource-z`

This keeps compatibility practical.

## 10. Profiles are adapters, not forks

A profile may:

- map source symbols;
- define scopes and types;
- define hooks;
- define resource locator schemes;
- require extension operators;
- provide source metadata;
- define reset/default conventions.

A profile MUST NOT silently change the meaning of a core operator.

Different behavior requires namespaced semantics.

## 11. Future binary header guidance

The supplied recipe-file reference uses magic/version fields, flags, hash/compression identifiers and reserved bytes for future compatibility.

The eventual `.ktnroute` binary SHOULD similarly define at minimum:

- magic `KTRF`;
- binary format version;
- explicit header size;
- flags/capabilities;
- section directory;
- integrity algorithm identifiers;
- explicit endianness;
- extension/reserved capacity;
- deterministic section ordering.

Exact byte widths and offsets are deliberately deferred.

## 12. Compatibility examples

### Optional debug extension

An old runtime may ignore/preserve it if routing semantics are unchanged.

### New required predicate operator

An old runtime sees the unsupported required feature and refuses to load.

### New game profile using only existing core semantics

A runtime may load it if it supports the profile/resource adapter without any core format revision.

### Shiny Days reveals a new routing primitive

Preferred process:

```text
discover primitive
-> document exact semantics
-> allocate namespace and required feature
-> add namespaced IR operator/effect
-> implement adapter
-> add conformance tests
```

Promote it into `ktrf:` core only if it proves generally useful and the core specification deliberately standardizes it.
