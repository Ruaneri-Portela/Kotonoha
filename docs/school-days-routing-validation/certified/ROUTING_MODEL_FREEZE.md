# School Days HQ — Routing Model Freeze

Status: **PASS**

Scope: Normal New Game routing oracle.

## Frozen counts

- Routes: **55**
- Nodes / SceneKeys: **1857**
- Reachable router transitions: **2458**
- Conditions: **1464**
- Effects: **6183**
- Feeling resolutions: **772**
- Terminal transitions: **23**
- Non-terminal `callback_38` handoffs: **47**
- Terminal `callback_38` handoffs: **23**

## Structural checks

- `PASS` route_count — 55 == 55
- `PASS` node_count — 1857 == 1857
- `PASS` transition_count — 2458 == 2458
- `PASS` condition_count — 1464 == 1464
- `PASS` effect_count — 6183 == 6183
- `PASS` feeling_resolution_count — 772 == 772
- `PASS` offset_first_zero
- `PASS` offset_last_matches_nodes
- `PASS` offsets_monotonic
- `PASS` scene_keys_unique
- `PASS` routing_only_catalog — 03/03-B2-A00, 03/03-KB-E00
- `PASS` node_transition_slices
- `PASS` transition_ids_unique
- `PASS` transition_condition_slices
- `PASS` transition_effect_slices
- `PASS` nonterminal_destinations_valid
- `PASS` condition_kind_catalog — ['Callback34', 'Choice', 'FlagOr', 'GlobalValue', 'SessionComparison', 'SessionValue']
- `PASS` compare_op_catalog — ['Eq', 'Gt', 'Le', 'Ne']
- `PASS` effect_kind_catalog — ['Callback', 'RegisterEnding', 'SetGlobalConst', 'SetSessionConst', 'SetSessionFromSession']
- `PASS` feeling_resolution_slices
- `PASS` all_transitions_owned_by_node — 2458 / 2458
- `PASS` terminal_transition_count — 23
- `PASS` terminal_callback38_count — 23
- `PASS` nonterminal_callback38_count — 47
- `PASS` router_checkpoint_sd-ep1-r0-l00-handoff
- `PASS` test_checkpoint_sd-ep1-r0-l00-handoff
- `PASS` router_checkpoint_sd-ending16-r49-i02-handoff
- `PASS` test_checkpoint_sd-ending16-r49-i02-handoff
- `PASS` temporary_ktn_diag_removed
- `PASS` canonical_file_exists:CMakeLists.txt
- `PASS` canonical_file_exists:include/Kotonoha/SchoolDaysRouter.hpp
- `PASS` canonical_file_exists:include/Kotonoha/Kotonoha.hpp
- `PASS` canonical_file_exists:src/SchoolDaysRouter.cpp
- `PASS` canonical_file_exists:src/SchoolDaysRouteData.generated.inc
- `PASS` canonical_file_exists:src/Kotonoha.cpp
- `PASS` canonical_file_exists:tests/SchoolDaysFullRouterTest.cpp
- `PASS` canonical_file_exists:tests/SchoolDaysHandoffRouterTest.cpp
- `PASS` canonical_file_exists:tests/SchoolDaysDevCheckpointTest.cpp
- `PASS` canonical_file_exists:tools/schooldays-routing-verifier/audit_terminal_callbacks.py
- `PASS` canonical_file_exists:tools/schooldays-routing-verifier/freeze_route_model.py
- `PASS` canonical_file_exists:tools/schooldays-routing-verifier/run_freeze_audit.ps1

## Oracle rule

Any future routing serialization or KTRF implementation must reproduce
this frozen model without changing the certified observable routing
semantics. A deliberate routing-model change requires a new freeze
version rather than silently replacing this oracle.
