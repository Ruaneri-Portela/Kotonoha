# KTRF Data Model v0.1

This document defines the nine primary KTRF routing entities and one supporting EntryPoint record.

Every entity has a stable string `id`. IDs are document-local semantic identifiers unless a profile explicitly establishes a wider identity convention.

## 1. Node

A **Node** is a logical state in the routing graph at which routing may later be evaluated.

A Node is not a file and is not a timeline.

Required fields:

- `id` — stable unique Node ID.
- `kind` — namespace-qualified logical role.
- `resources` — zero or more ResourceLocator references.

Core kinds:

- `ktrf:scene` — ordinary playable/logical routing node.
- `ktrf:dispatcher` — logical node that may have no physical resource and is expected to route when the host invokes the applicable trigger.
- `ktrf:synthetic` — intentionally compiler/profile-created logical node.

Profiles MAY define additional namespaced kinds through required features.

Optional fields:

- `label`;
- `metadata`;
- `extensions`.

Invariants:

- Node IDs MUST be unique.
- Transition sources/destinations MUST resolve to Node IDs.
- Missing physical resources do not invalidate a Node.

## 2. Transition

A **Transition** is an ordered routing branch from one Node.

It may move to another Node or terminate routing.

Required fields:

- `id`;
- `source`;
- `priority`;
- ordered `effects`;
- `terminal`.

Conditional fields:

- `destination` is required when `terminal` is false and forbidden when true;
- `predicate` is an optional Expression reference; omitted means true;
- `triggers` is optional and defaults semantically to `["ktrf:next"]`.

Transitions are evaluated by priority and first-match wins.

Original source-engine IDs belong in metadata if they are not already used as deterministic KTRF IDs.

## 3. Expression

An **Expression** is a side-effect-free predicate/value computation.

Expressions are named records forming a DAG rather than requiring deeply nested JSON trees.

Required fields:

- `id`;
- namespace-qualified `op`;
- ordered `args`.

Argument forms:

- literal value;
- Variable reference;
- Expression reference;
- generic entity reference containing an entity class and ID.

Core operators initially reserved:

- `ktrf:const`
- `ktrf:var`
- `ktrf:eq`
- `ktrf:ne`
- `ktrf:lt`
- `ktrf:le`
- `ktrf:gt`
- `ktrf:ge`
- `ktrf:and`
- `ktrf:or`
- `ktrf:not`

Profile operators require declared namespaces and features.

Invariants:

- expression graphs MUST be acyclic;
- predicate expressions MUST yield `ktrf:bool`;
- expressions MUST NOT mutate runtime state.

## 4. Effect

An **Effect** is an ordered state mutation or externally observable routing action.

Required fields:

- `id`;
- namespace-qualified `op`;
- operator-specific `args`.

Initial core effect families:

- `ktrf:set` — assign a value to a Variable;
- `ktrf:copy` — copy one Variable into another;
- `ktrf:add` — numeric addition;
- `ktrf:register-ending` — register an Ending;
- `ktrf:call-hook` — invoke an ExternalHook contract.

Not every source primitive must map one-to-one to core Effects. If generic lowering would lose semantics, a profile MUST use a namespaced operator.

Invariants:

- effects execute in listed order;
- unknown required operators cause rejection;
- hook effects do not infer frontend meaning.

## 5. Variable

A **Variable** is typed runtime state referenced by Expressions or Effects.

Required fields:

- `id`;
- namespace-qualified `type`;
- namespace-qualified `scope`;
- `default`.

Core types:

- `ktrf:bool`
- `ktrf:int32`
- `ktrf:uint32`
- `ktrf:float32`
- `ktrf:float64`
- `ktrf:string`
- `ktrf:bytes`

Profiles may define feature-gated types.

In JSON IR, `ktrf:bytes` is represented as a base64 string. The future binary container may store the same semantic value directly as bytes.

Core scopes begin with:

- `ktrf:session`
- `ktrf:global`

Profiles may define scopes such as `overflow.sdhq:history`.

The source-engine symbol belongs in metadata/profile mapping.

Invariants:

- every Variable reference MUST resolve;
- assignment must respect type semantics unless an operator contract defines conversion.

## 6. Choice

A **Choice** describes a discrete user/runtime decision associated with a Node.

Choice selection and routing are separate operations.

Required fields:

- `id`;
- owning `node`;
- `result_variable`;
- ordered `options`;
- `routing_policy`.

In v0.1, `routing_policy` MUST be `ktrf:deferred`.

Each option contains:

- local stable `id`;
- result `value`;
- ordered `effects`;
- optional presentation metadata.

A Choice MAY define `timeout` with its own result value and ordered Effects.

Commit semantics:

1. write the result;
2. execute option/timeout effects;
3. do not evaluate Transitions;
4. wait for a later routing trigger.

## 7. Ending

An **Ending** is an identified terminal outcome that routing can register.

Required:

- `id`.

Optional:

- source-engine `code`;
- human-readable `label`;
- metadata/extensions.

Registration is explicit through an Effect.

Terminal Transition does not imply a particular Ending. This distinction is required because a routing system may contain terminal Transitions whose Ending was registered earlier.

## 8. ExternalHook

An **ExternalHook** is an opaque request from routing logic to the host engine or adapter.

It preserves source behavior that is externally meaningful but not generic routing semantics.

Required fields:

- `id`;
- namespace-qualified `symbol`;
- versioned `contract`.

Optional:

- arguments schema;
- metadata/extensions.

Example symbol:

`overflow.sdhq:callback_38`

KTRF core does not rename it to `show-save-menu`.

Unknown required hook contracts cause rejection.

## 9. ResourceLocator

A **ResourceLocator** associates a logical Node with a host-resolvable physical resource.

Required fields:

- `id`;
- namespace-qualified `scheme`;
- scheme-defined `value`.

Optional:

- `media_type`;
- `required`;
- metadata/extensions.

Core schemes:

- `ktrf:path` — normalized logical path;
- `ktrf:uri` — URI.

Profiles may define archive entry, scene database or engine object schemes.

A ResourceLocator does not define routing identity.

Multiple Nodes may refer to one resource, and a Node may have no resource.

## Supporting record: EntryPoint

An **EntryPoint** identifies a supported way to initialize execution at a Node.

Required:

- `id`;
- `node`.

Optional:

- `trigger`;
- metadata/extensions.

Examples:

- New Game;
- chapter/replay entry;
- profile-defined debug/test entry.

The core does not assume that the first Node in an array is the entry point.

## Reference summary

```text
EntryPoint ----> Node <------ Transition ------> destination Node
                 |                |
                 |                +------> Expression
                 |                |
                 |                +------> Effect
                 |
                 +------> ResourceLocator
                 |
                 +------> Choice ------> Effect

Expression ----> Variable
Effect --------> Variable
Effect --------> Ending
Effect --------> ExternalHook
```

## School Days mapping note

The frozen School Days model maps as follows:

- `(ROUTE, SCENE)` becomes profile metadata, not KTRF identity;
- `SceneKey` becomes a stable Node alias/ID source;
- generated conditions lower into Expressions;
- generated effects lower into Effects;
- `SetSELECT` plus feeling resolution lowers into Choice plus option Effects;
- `RegisterEnding` lowers into Ending registration;
- callbacks become ExternalHooks;
- the two router-only Nodes are `ktrf:dispatcher` Nodes with no physical resource;
- Route 0 / Scene 0 becomes the New Game EntryPoint for the Normal New Game profile.
