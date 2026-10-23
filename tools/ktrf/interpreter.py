#!/usr/bin/env python3
"""Reference interpreter for KTRF Canonical Routing IR v0.1.

This interpreter executes the semantic IR, not the future .ktnroute binary.
It intentionally implements only KTRF core v0.1 operators. Profile-specific
operators/hooks remain opaque unless represented through core operations.

The interpreter exists for conformance work:

    Canonical Routing IR -> executable semantic model

It is not yet Kotonoha's production router.
"""

from __future__ import annotations

import argparse
import copy
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Mapping, Sequence


class KtrfRuntimeError(RuntimeError):
    pass


@dataclass(frozen=True)
class HookCall:
    hook_id: str
    symbol: str
    arguments: tuple[Any, ...]


@dataclass(frozen=True)
class TransitionResult:
    transition_id: str
    source: str
    destination: str | None
    terminal: bool
    hook_calls: tuple[HookCall, ...]
    registered_endings: tuple[str, ...]


class Interpreter:
    """Executable reference semantics for KTRF Routing IR v0.1."""

    def __init__(self, document: Mapping[str, Any]):
        self.doc = document
        self.variables = self._index("variables")
        self.expressions = self._index("expressions")
        self.effects = self._index("effects")
        self.nodes = self._index("nodes")
        self.endings = self._index("endings")
        self.hooks = self._index("external_hooks")
        self.choices = self._index("choices")
        self.entry_points = self._index("entry_points")
        self.transitions = self._index("transitions")

        self.transitions_by_source: dict[str, list[Mapping[str, Any]]] = {}
        for row in self._records("transitions"):
            source = row.get("source")
            if isinstance(source, str):
                self.transitions_by_source.setdefault(source, []).append(row)
        for rows in self.transitions_by_source.values():
            rows.sort(key=lambda r: (int(r.get("priority", 0)), str(r.get("id", ""))))

        self.choices_by_node: dict[str, list[Mapping[str, Any]]] = {}
        for row in self._records("choices"):
            node = row.get("node")
            if isinstance(node, str):
                self.choices_by_node.setdefault(node, []).append(row)

        self.values: dict[str, Any] = {}
        self.current_node: str | None = None
        self.terminal = False
        self.ending_registrations: list[str] = []
        self.hook_history: list[HookCall] = []
        self._committed_choices: dict[str, Any] = {}

    def _records(self, collection: str) -> list[Mapping[str, Any]]:
        value = self.doc.get(collection, [])
        if not isinstance(value, list):
            return []
        return [row for row in value if isinstance(row, Mapping)]

    def _index(self, collection: str) -> dict[str, Mapping[str, Any]]:
        out: dict[str, Mapping[str, Any]] = {}
        for row in self._records(collection):
            row_id = row.get("id")
            if isinstance(row_id, str):
                out[row_id] = row
        return out

    def reset(self, entry_id: str | None = None) -> None:
        self.values = {
            var_id: copy.deepcopy(row.get("default"))
            for var_id, row in self.variables.items()
        }
        self.terminal = False
        self.ending_registrations = []
        self.hook_history = []
        self._committed_choices = {}

        if entry_id is None:
            entries = self._records("entry_points")
            if len(entries) != 1:
                raise KtrfRuntimeError(
                    "entry_id is required when the document does not have exactly one entry point"
                )
            entry = entries[0]
        else:
            entry = self.entry_points.get(entry_id)
            if entry is None:
                raise KtrfRuntimeError(f"unknown entry point {entry_id!r}")

        node = entry.get("node")
        if not isinstance(node, str) or node not in self.nodes:
            raise KtrfRuntimeError(f"entry point has invalid node {node!r}")
        self.current_node = node

    def read_variable(self, variable_id: str) -> Any:
        if variable_id not in self.variables:
            raise KtrfRuntimeError(f"unknown variable {variable_id!r}")
        return self.values[variable_id]

    def write_variable(self, variable_id: str, value: Any) -> None:
        if variable_id not in self.variables:
            raise KtrfRuntimeError(f"unknown variable {variable_id!r}")
        self.values[variable_id] = value

    def _eval_value(self, value: Any) -> Any:
        if not isinstance(value, Mapping):
            raise KtrfRuntimeError(f"invalid value reference {value!r}")
        kind = value.get("kind")
        if kind == "literal":
            return copy.deepcopy(value.get("value"))
        if kind == "variable":
            ref = value.get("ref")
            if not isinstance(ref, str):
                raise KtrfRuntimeError("variable value reference has no ref")
            return self.read_variable(ref)
        if kind == "expression":
            ref = value.get("ref")
            if not isinstance(ref, str):
                raise KtrfRuntimeError("expression value reference has no ref")
            return self.eval_expression(ref)
        if kind == "entity":
            ref = value.get("ref")
            if not isinstance(ref, str):
                raise KtrfRuntimeError("entity value reference has no ref")
            return ref
        raise KtrfRuntimeError(f"unsupported value-reference kind {kind!r}")

    def eval_expression(self, expression_id: str) -> Any:
        row = self.expressions.get(expression_id)
        if row is None:
            raise KtrfRuntimeError(f"unknown expression {expression_id!r}")
        op = row.get("op")
        args = row.get("args", [])
        if not isinstance(args, list):
            raise KtrfRuntimeError(f"expression {expression_id!r} args are not an array")
        values = [self._eval_value(arg) for arg in args]

        if op in {"ktrf:const", "ktrf:var"}:
            return values[0]
        if op == "ktrf:eq":
            return values[0] == values[1]
        if op == "ktrf:ne":
            return values[0] != values[1]
        if op == "ktrf:lt":
            return values[0] < values[1]
        if op == "ktrf:le":
            return values[0] <= values[1]
        if op == "ktrf:gt":
            return values[0] > values[1]
        if op == "ktrf:ge":
            return values[0] >= values[1]
        if op == "ktrf:and":
            return all(bool(v) for v in values)
        if op == "ktrf:or":
            return any(bool(v) for v in values)
        if op == "ktrf:not":
            return not bool(values[0])
        raise KtrfRuntimeError(f"unsupported expression op {op!r}")

    def _apply_effect(
        self,
        effect_id: str,
        hook_calls: list[HookCall],
        newly_registered: list[str],
    ) -> None:
        row = self.effects.get(effect_id)
        if row is None:
            raise KtrfRuntimeError(f"unknown effect {effect_id!r}")
        op = row.get("op")
        args = row.get("args", {})
        if not isinstance(args, Mapping):
            raise KtrfRuntimeError(f"effect {effect_id!r} args are not an object")

        if op == "ktrf:set":
            target = args.get("target")
            if not isinstance(target, str):
                raise KtrfRuntimeError(f"ktrf:set {effect_id!r} has invalid target")
            self.write_variable(target, self._eval_value(args.get("value")))
            return

        if op == "ktrf:copy":
            target = args.get("target")
            source = args.get("source")
            if not isinstance(target, str) or not isinstance(source, str):
                raise KtrfRuntimeError(f"ktrf:copy {effect_id!r} has invalid operands")
            self.write_variable(target, copy.deepcopy(self.read_variable(source)))
            return

        if op == "ktrf:add":
            target = args.get("target")
            if not isinstance(target, str):
                raise KtrfRuntimeError(f"ktrf:add {effect_id!r} has invalid target")
            self.write_variable(target, self.read_variable(target) + self._eval_value(args.get("value")))
            return

        if op == "ktrf:register-ending":
            ending = args.get("ending")
            if not isinstance(ending, str) or ending not in self.endings:
                raise KtrfRuntimeError(f"ktrf:register-ending {effect_id!r} has invalid ending")
            self.ending_registrations.append(ending)
            newly_registered.append(ending)
            return

        if op == "ktrf:call-hook":
            hook_id = args.get("hook")
            if not isinstance(hook_id, str) or hook_id not in self.hooks:
                raise KtrfRuntimeError(f"ktrf:call-hook {effect_id!r} has invalid hook")
            raw_arguments = args.get("arguments", [])
            if not isinstance(raw_arguments, list):
                raise KtrfRuntimeError(f"ktrf:call-hook {effect_id!r} arguments are not an array")
            values = tuple(self._eval_value(v) for v in raw_arguments)
            symbol = self.hooks[hook_id].get("symbol")
            if not isinstance(symbol, str):
                raise KtrfRuntimeError(f"hook {hook_id!r} has invalid symbol")
            call = HookCall(hook_id=hook_id, symbol=symbol, arguments=values)
            hook_calls.append(call)
            self.hook_history.append(call)
            return

        raise KtrfRuntimeError(f"unsupported effect op {op!r}")

    def _choice_for_current_node(self, choice_id: str | None) -> Mapping[str, Any]:
        if self.current_node is None:
            raise KtrfRuntimeError("interpreter has not been reset")
        if choice_id is not None:
            row = self.choices.get(choice_id)
            if row is None:
                raise KtrfRuntimeError(f"unknown choice {choice_id!r}")
            if row.get("node") != self.current_node:
                raise KtrfRuntimeError(
                    f"choice {choice_id!r} belongs to {row.get('node')!r}, current node is {self.current_node!r}"
                )
            return row

        rows = self.choices_by_node.get(self.current_node, [])
        if len(rows) != 1:
            raise KtrfRuntimeError(
                f"current node {self.current_node!r} has {len(rows)} choices; choice_id required"
            )
        return rows[0]

    def _has_uncommitted_deferred_choice(self) -> bool:
        if self.current_node is None:
            return False
        for row in self.choices_by_node.get(self.current_node, []):
            if row.get("routing_policy") != "ktrf:deferred":
                continue
            choice_id = row.get("id")
            if not isinstance(choice_id, str):
                raise KtrfRuntimeError("choice at current node has invalid ID")
            if choice_id not in self._committed_choices:
                return True
        return False

    def commit_choice(self, value: Any, choice_id: str | None = None) -> bool:
        if self.terminal:
            raise KtrfRuntimeError("cannot commit choice after terminal transition")
        row = self._choice_for_current_node(choice_id)
        cid = str(row.get("id"))

        if cid in self._committed_choices:
            return self._committed_choices[cid] == value

        selected: Mapping[str, Any] | None = None
        for option in row.get("options", []):
            if isinstance(option, Mapping) and option.get("value") == value:
                selected = option
                break
        if selected is None:
            timeout = row.get("timeout")
            if isinstance(timeout, Mapping) and timeout.get("value") == value:
                selected = timeout
        if selected is None:
            return False

        result_variable = row.get("result_variable")
        if not isinstance(result_variable, str):
            raise KtrfRuntimeError(f"choice {cid!r} has invalid result_variable")

        # Normative v0.1 deferred-choice commit order:
        # 1. write result; 2. run outcome effects; 3. do NOT route.
        self.write_variable(result_variable, copy.deepcopy(value))
        hook_calls: list[HookCall] = []
        endings: list[str] = []
        for effect_id in selected.get("effects", []):
            self._apply_effect(str(effect_id), hook_calls, endings)
        self._committed_choices[cid] = copy.deepcopy(value)
        return True

    def trigger(self, trigger: str = "ktrf:next") -> TransitionResult | None:
        if self.current_node is None:
            raise KtrfRuntimeError("interpreter has not been reset")
        if self.terminal:
            raise KtrfRuntimeError("cannot route after terminal transition")

        # A deferred Choice is a routing gate.  Merely having an outgoing
        # unconditional Transition must not allow the runtime to advance before
        # the Choice result has been committed and its outcome Effects applied.
        if self._has_uncommitted_deferred_choice():
            return None

        candidates = []
        for row in self.transitions_by_source.get(self.current_node, []):
            triggers = row.get("triggers")
            if triggers is None:
                triggers = ["ktrf:next"]
            if isinstance(triggers, list) and trigger in triggers:
                candidates.append(row)

        selected: Mapping[str, Any] | None = None
        for row in candidates:
            predicate = row.get("predicate")
            if predicate is None or bool(self.eval_expression(str(predicate))):
                selected = row
                break

        if selected is None:
            return None

        source = self.current_node
        hook_calls: list[HookCall] = []
        newly_registered: list[str] = []
        for effect_id in selected.get("effects", []):
            self._apply_effect(str(effect_id), hook_calls, newly_registered)

        is_terminal = selected.get("terminal") is True
        destination: str | None = None
        if is_terminal:
            self.terminal = True
        else:
            raw_destination = selected.get("destination")
            if not isinstance(raw_destination, str) or raw_destination not in self.nodes:
                raise KtrfRuntimeError(
                    f"transition {selected.get('id')!r} has invalid destination {raw_destination!r}"
                )
            destination = raw_destination
            self.current_node = destination
            self._committed_choices = {}

        transition_id = selected.get("id")
        if not isinstance(transition_id, str):
            raise KtrfRuntimeError("selected transition has invalid ID")

        return TransitionResult(
            transition_id=transition_id,
            source=source,
            destination=destination,
            terminal=is_terminal,
            hook_calls=tuple(hook_calls),
            registered_endings=tuple(newly_registered),
        )

    def snapshot(self) -> dict[str, Any]:
        return {
            "current_node": self.current_node,
            "terminal": self.terminal,
            "variables": copy.deepcopy(self.values),
            "ending_registrations": list(self.ending_registrations),
            "hook_history": [
                {
                    "hook_id": call.hook_id,
                    "symbol": call.symbol,
                    "arguments": list(call.arguments),
                }
                for call in self.hook_history
            ],
        }


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Execute a small KTRF IR smoke trace")
    parser.add_argument("input", type=Path, help="Canonical Routing IR JSON")
    parser.add_argument("--entry", default=None, help="entry-point ID")
    parser.add_argument("--trigger", default="ktrf:next", help="routing trigger")
    args = parser.parse_args(argv)

    document = json.loads(args.input.read_text(encoding="utf-8-sig"))
    interpreter = Interpreter(document)
    interpreter.reset(args.entry)
    result = interpreter.trigger(args.trigger)
    print(
        json.dumps(
            {
                "result": None if result is None else result.__dict__,
                "state": interpreter.snapshot(),
            },
            ensure_ascii=False,
            indent=2,
            default=lambda o: o.__dict__,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
