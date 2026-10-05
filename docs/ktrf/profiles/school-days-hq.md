# School Days HQ KTRF Profile v1.0

Status: **Design mapping draft**

Oracle:

- tag: `school-days-routing-oracle-v1`
- commit: `614461c2b14951ba117b9d2dedb4983cfa8ae8e6`

This profile explains how School Days HQ maps into generic KTRF concepts. It does not redefine KTRF core semantics.

## Namespace

Prefix:

`overflow.sdhq`

URI:

`urn:kotonoha:profile:overflow:school-days-hq`

## Entry point

Normal New Game:

- source coordinate: Route 0 / Scene 0;
- SceneKey: `00/00-00-A00`;
- KTRF entry ID: `sdhq:entry:new-game`;
- trigger: `ktrf:new-game`.

## Node mapping

Each recovered `(ROUTE, SCENE)` entry becomes one Node.

Recommended Node ID:

`sdhq:node:<SceneKey>`

`SceneKey`, route and scene remain profile metadata.

## Routing-only Nodes

These are `ktrf:dispatcher` Nodes without physical ResourceLocators:

- `03/03-B2-A00`
- `03/03-KB-E00`

They prove that Node identity cannot be physical-file identity.

## Variables

Recovered session/global/history/flag values become Variables.

Examples:

- `sdhq:var:ROUTE`
- `sdhq:var:SCENE`
- `sdhq:var:001`
- `sdhq:var:002`
- `sdhq:var:BS05SBI01`
- `sdhq:var:REP05_SB_I01`

Source symbols are retained as metadata.

## Conditions

Recovered condition families lower into generic Expressions where semantics match:

- Choice comparison -> Variable/literal comparison;
- SessionValue -> Variable/literal comparison;
- SessionComparison -> Variable/Variable comparison;
- GlobalValue -> Variable/literal comparison;
- Callback34 -> profile-declared Variable or exact profile expression;
- FlagOr -> core boolean composition when exact semantics are preserved.

Transition priority MUST preserve recovered first-match order.

## Effects

Recovered effects lower approximately as:

- `SetSessionConst` -> `ktrf:set`
- `SetSessionFromSession` -> `ktrf:copy`
- `SetGlobalConst` -> `ktrf:set`
- `RegisterEnding` -> `ktrf:register-ending`
- `Callback` -> `ktrf:call-hook`

Effect order MUST be preserved.

## Choices and feelings

`SetSELECT` nodes become Choices.

Result convention:

- `-2` pending;
- `-1` timeout;
- `0` first option;
- `1` second option.

Feeling deltas become Choice outcome Effects.

Routing remains deferred until `ktrf:next`.

## Endings

The oracle contains 22 reachable native Endings.

Ending registration is explicit and distinct from terminality.

Three terminal transitions do not directly execute `RegisterEnding`; validators MUST NOT require “every terminal transition registers an Ending”.

## Hooks

Recovered callbacks become ExternalHooks:

- `overflow.sdhq:callback_00`
- `overflow.sdhq:callback_2C`
- `overflow.sdhq:callback_38`

The static IR does not rename `callback_38` to a Save/Continue action.

The School Days runtime adapter may interpret it as an episode/terminal handoff based on separately validated runtime behavior.

## Resources

Physical ORS resources use ResourceLocators.

ORS-only physical scripts are not automatically Nodes in the Normal New Game routing graph.

## Conformance target

A School Days exporter/loader must preserve oracle semantics including:

- 55 source routes as profile grouping/metadata;
- 1,857 logical Nodes;
- 2,458 Normal New Game Transitions;
- first-match Transition order;
- 1,464 recovered conditions after semantic lowering;
- 6,183 recovered effects after semantic lowering;
- 302 feeling deltas;
- 772 feeling resolutions;
- 22 reachable Endings;
- 47 non-terminal `callback_38` handoffs;
- 23 terminal `callback_38` handoffs;
- both routing-only dispatchers;
- all six known dead transitions as structurally preserved model data.

Exact Expression count after generic lowering is not required to match source condition count if semantics remain identical.

Semantic equivalence, not accidental source-array shape, is the conformance requirement.
