# Canonical Routing IR v0.1

The Routing IR is the human-readable source/interchange representation of KTRF semantics.

It is validated structurally by:

`schemas/ktrf-routing-ir.schema.json`

## 1. Top-level structure

```json
{
  "format": "ktrf-routing-ir",
  "ir_version": "0.1.0",
  "document_id": "example.game.routing",
  "profile": {
    "id": "example.game",
    "version": "1.0.0"
  },
  "features": {
    "required": [],
    "optional": []
  },
  "namespaces": [],
  "entry_points": [],
  "variables": [],
  "resource_locators": [],
  "external_hooks": [],
  "endings": [],
  "expressions": [],
  "effects": [],
  "choices": [],
  "nodes": [],
  "transitions": [],
  "metadata": {},
  "extensions": []
}
```

## 2. Stable IDs

IDs:

- are strings;
- are case-sensitive;
- MUST be unique within their collection;
- SHOULD be deterministic when generated;
- MUST NOT depend on binary offsets.

Recommended syntax:

`[A-Za-z0-9][A-Za-z0-9._:/-]*`

Examples:

- `sdhq:node:03/03-KB-E00`
- `sdhq:t917`
- `sdhq:var:001`
- `sdhq:hook:callback_38`

## 3. Qualified names

Semantic vocabulary names use:

`prefix:local-name`

The `ktrf` prefix is reserved.

All non-core prefixes are declared in `namespaces`.

Example:

```json
{
  "prefix": "overflow.sdhq",
  "uri": "urn:kotonoha:profile:overflow:school-days-hq"
}
```

The URI is an identifier and need not be network-fetchable.

## 4. Features

Requirement:

```json
{
  "id": "ktrf:expression-core",
  "version": "1.0.0"
}
```

`features.required` means unsupported => load failure.

`features.optional` may be ignored only when the feature specification explicitly guarantees that doing so cannot change required routing semantics.

## 5. Profile

```json
{
  "id": "overflow.school-days-hq",
  "version": "1.0.0"
}
```

Profiles describe source-game conventions and adapter expectations.

A profile cannot silently redefine core semantics.

## 6. Entry points

```json
{
  "id": "sdhq:entry:new-game",
  "node": "sdhq:node:00/00-00-A00",
  "trigger": "ktrf:new-game"
}
```

Entry points prevent assumptions such as “array index zero is always New Game”.

## 7. Variables

```json
{
  "id": "sdhq:var:001",
  "type": "ktrf:int32",
  "scope": "ktrf:session",
  "default": 0,
  "metadata": {
    "source_symbol": "001"
  }
}
```

## 8. Expressions

Arguments are tagged.

Literal:

```json
{ "kind": "literal", "value": 0 }
```

Variable:

```json
{ "kind": "variable", "ref": "sdhq:var:001" }
```

Expression:

```json
{ "kind": "expression", "ref": "sdhq:expr:001_le_002" }
```

Generic entity:

```json
{ "kind": "entity", "entity": "ending", "ref": "sdhq:ending:16" }
```

Expression example:

```json
{
  "id": "sdhq:expr:001_le_002",
  "op": "ktrf:le",
  "args": [
    { "kind": "variable", "ref": "sdhq:var:001" },
    { "kind": "variable", "ref": "sdhq:var:002" }
  ],
  "result_type": "ktrf:bool"
}
```

## 9. Effects

Hook call:

```json
{
  "id": "sdhq:effect:call_callback38",
  "op": "ktrf:call-hook",
  "args": {
    "hook": "sdhq:hook:callback_38",
    "arguments": []
  }
}
```

Assignment:

```json
{
  "id": "sdhq:effect:set_route_1",
  "op": "ktrf:set",
  "args": {
    "target": "sdhq:var:ROUTE",
    "value": { "kind": "literal", "value": 1 }
  }
}
```

## 10. Nodes

Routing-only dispatcher:

```json
{
  "id": "sdhq:node:03/03-KB-E00",
  "kind": "ktrf:dispatcher",
  "resources": [],
  "metadata": {
    "scene_key": "03/03-KB-E00",
    "route": 15,
    "scene": 31
  }
}
```

## 11. Transitions

```json
{
  "id": "sdhq:t907",
  "source": "sdhq:node:03/03-KB-E00",
  "destination": "sdhq:node:03/03-KB-G00",
  "priority": 0,
  "predicate": "sdhq:expr:001_le_002",
  "effects": [
    "sdhq:effect:set_route_15",
    "sdhq:effect:set_scene_48"
  ],
  "terminal": false,
  "triggers": ["ktrf:next"]
}
```

`priority` is explicit even if recovered source data happens to be stored in the same order.

## 12. Choices

```json
{
  "id": "sdhq:choice:00/00-00-A03",
  "node": "sdhq:node:00/00-00-A03",
  "result_variable": "sdhq:var:choice_result",
  "routing_policy": "ktrf:deferred",
  "options": [
    {
      "id": "option0",
      "value": 0,
      "effects": ["sdhq:effect:feeling_delta_example"]
    },
    {
      "id": "option1",
      "value": 1,
      "effects": []
    }
  ],
  "timeout": {
    "value": -1,
    "effects": []
  }
}
```

## 13. Endings

```json
{
  "id": "sdhq:ending:16",
  "code": 16
}
```

Ending identity is separate from terminal transitions.

## 14. External hooks

```json
{
  "id": "sdhq:hook:callback_38",
  "symbol": "overflow.sdhq:callback_38",
  "contract": "overflow.sdhq.callback/1.0.0"
}
```

The generic IR does not rename this hook to a frontend operation.

## 15. Resource locators

```json
{
  "id": "sdhq:resource:05/05-SB-I02",
  "scheme": "ktrf:path",
  "value": "05/05-SB/05-SB-I02.ENG.ORS",
  "required": true
}
```

## 16. Extensions

```json
{
  "namespace": "overflow.sdhq",
  "type": "routing-profile-data",
  "required_feature": "overflow.sdhq:profile-data",
  "data": {}
}
```

Extensions MUST declare the namespace owning their semantics.

Unsupported `required_feature` => reject.

## 17. Validation layers

JSON Schema validates structural shape.

A semantic validator must additionally check:

- cross-reference existence;
- ID uniqueness;
- expression acyclicity;
- operator arity/types;
- assignment type safety;
- unique transition priorities per source/trigger;
- terminal/destination rules;
- namespace declarations;
- required feature support;
- Effect order;
- Choice value compatibility;
- profile invariants.

Therefore `JSON Schema PASS` is necessary but not sufficient for semantic validity.

## 18. Canonical writer rules

Reference writers SHOULD:

- emit UTF-8 without BOM;
- emit LF;
- use two-space indentation;
- use deterministic object-key ordering where practical;
- sort declaration collections by stable ID when order is not semantic;
- preserve Effect reference order;
- preserve Choice option order;
- preserve explicit Transition priority.

A later integrity specification will define canonical hashing.
