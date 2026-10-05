# KTRF Conceptual Specification v0.1

Status: **Draft**

The terms **MUST**, **MUST NOT**, **SHOULD**, **SHOULD NOT** and **MAY** are normative requirements for KTRF implementations.

## 1. Purpose

KTRF represents a static routing program for a game or interactive narrative runtime.

A routing program describes:

- logical routing states;
- routing entry points;
- transition priority;
- predicates;
- state variables;
- ordered effects;
- choices and delayed choice effects;
- ending registration;
- external runtime hooks;
- associations between logical states and physical resources.

KTRF does not require the source engine to use routes, scenes, DLL callbacks, ORS files or any other School Days-specific concept.

## 2. Layering

KTRF is separated into three layers:

1. **Canonical Routing IR** — human-readable JSON carrying the semantic model.
2. **KTRF semantic model** — the normative abstract entities and execution rules.
3. **Future `.ktnroute` binary container** — a compact serialization defined only after the semantic model and IR stabilize.

A binary layout MUST NOT redefine semantics established by the semantic model.

## 3. Design principles

### 3.1 Identity and versioning

Every Routing IR document MUST identify:

- the IR format;
- the IR version;
- the game/profile mapping;
- required and optional features;
- declared namespaces.

The future binary container SHOULD follow the same broad engineering principles visible in the reference recipe-file documentation: unambiguous magic, explicit version, flags/capabilities, typed sections or records, integrity metadata and future-compatible reserved/extension capacity.

### 3.2 Stable semantic references

Semantic references MUST use stable string IDs.

Array indexes, binary offsets and section positions are serialization details and MUST NOT become semantic identity.

### 3.3 Logical versus physical separation

A Node is a logical routing state.

A Node MAY have zero, one or multiple ResourceLocators.

Therefore:

`Node != physical script file`

This is required by the School Days oracle, which contains logical dispatcher nodes with no physical ORS resource.

### 3.4 Ordered transitions

Transitions leaving the same Node for the same trigger are evaluated in ascending `priority`.

The first transition whose predicate evaluates to true is selected.

Two canonical transitions MUST NOT have the same priority for the same source Node and overlapping trigger set.

Array order MUST NOT substitute for explicit priority.

### 3.5 Ordered effects

Effects attached to a Transition or Choice outcome execute in listed order.

Effect order is semantically significant.

### 3.6 Delayed choice routing

Selecting a Choice MUST NOT implicitly evaluate routing transitions.

A `ktrf:deferred` Choice also acts as a routing gate. While a deferred Choice associated with the current Node has not been committed, a routing trigger MUST NOT evaluate outgoing Transition predicates or select an unconditional Transition. The routing request is unresolved/blocked and the current Node and declared state remain unchanged.

Committing the Choice releases that gate only after the selected option/timeout result has been written and its ordered Choice Effects have completed. The runtime therefore owns explicit current/committed Choice state in addition to the declared result Variable value. A raw Variable write by itself does not constitute a Choice commit.

Core sequence:

```text
choice offered
  -> routing remains blocked while Choice is uncommitted
  -> choice result committed
  -> choice outcome effects applied
  -> deferred routing gate released
  -> timeline/runtime continues
  -> host emits routing trigger
  -> transition predicates evaluated
  -> first matching transition selected
  -> transition effects applied
  -> destination becomes current Node unless terminal
```

This rule is mandatory because it is observable in the School Days oracle.

### 3.7 Opaque external semantics

Unknown engine callbacks MUST be preserved as ExternalHooks instead of being translated into guessed UI behavior.

Frontend interpretation belongs to an adapter/runtime layer.

### 3.8 Fail closed on required semantics

A consumer MUST reject a document if it does not support a feature listed in `features.required`.

A consumer MUST NOT silently approximate an unknown required expression operator, effect operator, hook contract, locator scheme or extension.

### 3.9 Static routing versus persistence

KTRF describes a static routing program.

Save-state serialization is a separate format concern.

A save system may snapshot KTRF runtime state, but save-file syntax is not part of KTRF.

## 4. Runtime state

At minimum, a KTRF runtime owns:

- a selected EntryPoint or explicitly supplied initial Node;
- current Node ID;
- declared Variable values;
- current/committed Choice state where applicable;
- ending registrations or equivalent profile state;
- adapter-defined external state required by supported features.

A runtime MAY maintain additional bookkeeping as long as it does not alter KTRF-visible semantics.

## 5. Entry points

A document MAY expose multiple named EntryPoints such as New Game, chapter start, replay or test entry.

KTRF does not assume a single universal first Node.

An EntryPoint binds a stable entry ID to a Node and optional host trigger.

Source-game route numbers are not entry identity.

## 6. Routing triggers

KTRF v0.1 defines core triggers:

- `ktrf:new-game` — host selects a New Game entry point.
- `ktrf:next` — host requests route evaluation after the current logical/timeline unit.

A host invokes `ktrf:next` when the source game would evaluate routing, such as School Days `[Next]`.

Profiles MAY introduce additional trigger names through namespaced required features.

## 7. Predicate evaluation

A Transition references zero or one Expression.

- no predicate means unconditional true;
- a referenced predicate MUST evaluate to `ktrf:bool`;
- expressions MUST be side-effect free;
- expression graphs MUST be acyclic.

The operator set is extensible and feature-gated.

## 8. Transition selection

For current Node and active trigger:

1. if the current Node has any uncommitted `ktrf:deferred` Choice, return `unresolved` without evaluating outgoing Transitions;
2. collect outgoing Transitions accepting the trigger;
3. sort by ascending `priority`;
4. evaluate predicates in that order;
5. select the first true Transition;
6. if none match, return `unresolved`;
7. execute selected Transition Effects in listed order;
8. if `terminal` is false, set current Node to `destination`;
9. if `terminal` is true, no destination is required.

A compiler MUST preserve this priority and deferred-Choice gate semantics exactly.

## 9. Terminal routing

A terminal Transition:

- MUST set `terminal: true`;
- MUST NOT specify a destination;
- MAY register an Ending through an Effect;
- MAY invoke ExternalHooks;
- does not prescribe whether the process exits, returns to title, saves, shows a prompt or launches another frontend state.

Those are adapter/frontend concerns.

## 10. Resource association

ResourceLocators are optional and non-authoritative for logical routing.

A consumer may resolve a ResourceLocator into a script, media object, archive entry, URI, database key or engine object.

Failure to resolve an optional resource MUST NOT rewrite the logical routing graph.

## 11. Canonical IR

The canonical IR is UTF-8 JSON.

Semantic identity depends on declared IDs and values, not JSON whitespace or object member order.

The reference writer SHOULD emit:

- UTF-8 without BOM;
- LF line endings;
- two-space indentation;
- deterministic object-key ordering where practical;
- declarations sorted by stable ID when array order is not semantic;
- semantic arrays in semantic order.

A later integrity document may define a byte-canonical JSON hash profile. v0.1 does not make JSON byte identity normative.

## 12. Compatibility boundary

KTRF core MUST remain independent from:

- `RouteProcSDHQ.dll`;
- source variable names such as `ROUTE`, `SCENE` or `001`;
- School Days callback numbers;
- ORS path conventions;
- any one Days-series save format.

Game-specific knowledge belongs in profiles, namespaces, extensions and adapters.

## 13. Acceptance criterion

The School Days profile is accepted only when:

```text
School Days v1 oracle
  -> canonical Routing IR
  -> KTRF compiler/loader
  -> decoded semantic model
  -> differential verifier
  -> zero semantic divergences
```

Until that passes, the frozen generated C++ router remains the executable oracle.
