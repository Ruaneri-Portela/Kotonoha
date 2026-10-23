#!/usr/bin/env python3
"""Semantic validator for KTRF Canonical Routing IR v0.1.

Validation is intentionally split into two layers:

1. JSON Schema validates structural shape.
2. This module validates semantic invariants that JSON Schema cannot express
   reliably, including cross references, expression DAGs, transition priority,
   type compatibility, namespace use and core operator contracts.

The validator does not execute routing and does not infer game-specific
semantics. Namespaced profile semantics remain opaque unless a future profile
validator adds stronger checks.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable, Mapping, Sequence


QNAME_RE = re.compile(r"^([A-Za-z][A-Za-z0-9._-]*):([A-Za-z0-9][A-Za-z0-9._/-]*)$")
SEMVER_RE = re.compile(r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?$")

CORE_TYPES = {
    "ktrf:bool",
    "ktrf:int32",
    "ktrf:uint32",
    "ktrf:float32",
    "ktrf:float64",
    "ktrf:string",
    "ktrf:bytes",
}
CORE_SCOPES = {"ktrf:session", "ktrf:global"}
CORE_NODE_KINDS = {"ktrf:scene", "ktrf:dispatcher", "ktrf:synthetic"}
CORE_TRIGGERS = {"ktrf:next", "ktrf:new-game"}
CORE_RESOURCE_SCHEMES = {"ktrf:path", "ktrf:uri"}
CORE_ROUTING_POLICIES = {"ktrf:deferred"}
CORE_EXPRESSION_OPS = {
    "ktrf:const",
    "ktrf:var",
    "ktrf:eq",
    "ktrf:ne",
    "ktrf:lt",
    "ktrf:le",
    "ktrf:gt",
    "ktrf:ge",
    "ktrf:and",
    "ktrf:or",
    "ktrf:not",
}
CORE_EFFECT_OPS = {
    "ktrf:set",
    "ktrf:copy",
    "ktrf:add",
    "ktrf:register-ending",
    "ktrf:call-hook",
}
CORE_FEATURES = {
    "ktrf:expression-core": "1.0.0",
    "ktrf:effects-core": "1.0.0",
    "ktrf:choices-deferred": "1.0.0",
    "ktrf:endings": "1.0.0",
    "ktrf:external-hooks": "1.0.0",
    "ktrf:resource-locators": "1.0.0",
}

COLLECTIONS = (
    "entry_points",
    "variables",
    "resource_locators",
    "external_hooks",
    "endings",
    "expressions",
    "effects",
    "choices",
    "nodes",
    "transitions",
)


@dataclass(frozen=True)
class Diagnostic:
    severity: str
    code: str
    path: str
    message: str

    def as_dict(self) -> dict[str, str]:
        return {
            "severity": self.severity,
            "code": self.code,
            "path": self.path,
            "message": self.message,
        }


class Validator:
    def __init__(
        self,
        document: Mapping[str, Any],
        *,
        enforce_capabilities: bool = False,
        supported_features: Mapping[str, str] | None = None,
    ) -> None:
        self.doc = document
        self.diagnostics: list[Diagnostic] = []
        self.indices: dict[str, dict[str, Mapping[str, Any]]] = {}
        self.namespaces: dict[str, str] = {}
        self.enforce_capabilities = enforce_capabilities
        self.supported_features = dict(CORE_FEATURES)
        if supported_features:
            self.supported_features.update(supported_features)

    def error(self, code: str, path: str, message: str) -> None:
        self.diagnostics.append(Diagnostic("error", code, path, message))

    def warning(self, code: str, path: str, message: str) -> None:
        self.diagnostics.append(Diagnostic("warning", code, path, message))

    def validate(self) -> list[Diagnostic]:
        self._build_indices()
        self._validate_namespaces()
        self._validate_features()
        self._validate_entry_points()
        self._validate_variables()
        self._validate_resources()
        self._validate_hooks()
        self._validate_endings()
        self._validate_expressions()
        self._validate_expression_cycles()
        self._validate_effects()
        self._validate_choices()
        self._validate_nodes()
        self._validate_transitions()
        self._validate_extensions()
        return self.diagnostics

    def _records(self, collection: str) -> Sequence[Mapping[str, Any]]:
        value = self.doc.get(collection, [])
        if not isinstance(value, list):
            return []
        return [x for x in value if isinstance(x, Mapping)]

    def _build_indices(self) -> None:
        for collection in COLLECTIONS:
            index: dict[str, Mapping[str, Any]] = {}
            for i, record in enumerate(self._records(collection)):
                record_id = record.get("id")
                if not isinstance(record_id, str):
                    continue
                if record_id in index:
                    self.error(
                        "KTRF1001",
                        f"/{collection}/{i}/id",
                        f"duplicate ID {record_id!r} in {collection}",
                    )
                else:
                    index[record_id] = record
            self.indices[collection] = index

        # IDs are scoped by entity collection in v0.1, but ambiguous reuse is
        # discouraged because it makes diagnostics and future binary debug maps
        # unnecessarily difficult to read.
        seen: dict[str, str] = {}
        for collection in COLLECTIONS:
            for record_id in self.indices[collection]:
                previous = seen.get(record_id)
                if previous is not None and previous != collection:
                    self.warning(
                        "KTRF1002",
                        f"/{collection}",
                        f"ID {record_id!r} is reused across {previous} and {collection}",
                    )
                else:
                    seen[record_id] = collection

    def _validate_namespaces(self) -> None:
        for i, item in enumerate(self.doc.get("namespaces", [])):
            if not isinstance(item, Mapping):
                continue
            prefix = item.get("prefix")
            uri = item.get("uri")
            if not isinstance(prefix, str):
                continue
            if prefix == "ktrf":
                self.error("KTRF1101", f"/namespaces/{i}/prefix", "ktrf namespace is reserved")
                continue
            if prefix in self.namespaces:
                self.error("KTRF1102", f"/namespaces/{i}/prefix", f"duplicate namespace prefix {prefix!r}")
            else:
                self.namespaces[prefix] = str(uri)

    def _check_qname(self, value: Any, path: str, *, known_core: set[str] | None = None) -> str | None:
        if not isinstance(value, str):
            return None
        match = QNAME_RE.match(value)
        if not match:
            self.error("KTRF1103", path, f"invalid qualified name {value!r}")
            return None
        prefix = match.group(1)
        if prefix == "ktrf":
            if known_core is not None and value not in known_core:
                self.error("KTRF1104", path, f"unknown core name {value!r} for IR v0.1")
        elif prefix not in self.namespaces:
            self.error("KTRF1105", path, f"namespace prefix {prefix!r} is not declared")
        return prefix

    @staticmethod
    def _parse_version(value: str) -> tuple[int, int, int] | None:
        if not SEMVER_RE.match(value):
            return None
        core = value.split("+", 1)[0].split("-", 1)[0]
        major, minor, patch = core.split(".")
        return int(major), int(minor), int(patch)

    @classmethod
    def _version_satisfies(cls, available: str, required: str) -> bool:
        av = cls._parse_version(available)
        req = cls._parse_version(required)
        if av is None or req is None:
            return False
        return av[0] == req[0] and av >= req

    def _feature_rows(self, kind: str) -> Sequence[Mapping[str, Any]]:
        features = self.doc.get("features", {})
        if not isinstance(features, Mapping):
            return []
        rows = features.get(kind, [])
        if not isinstance(rows, list):
            return []
        return [r for r in rows if isinstance(r, Mapping)]

    def _validate_features(self) -> None:
        required_ids: set[str] = set()
        optional_ids: set[str] = set()

        for kind, target in (("required", required_ids), ("optional", optional_ids)):
            for i, row in enumerate(self._feature_rows(kind)):
                feature_id = row.get("id")
                version = row.get("version")
                prefix = self._check_qname(feature_id, f"/features/{kind}/{i}/id")
                if isinstance(feature_id, str):
                    if feature_id in target:
                        self.error("KTRF1201", f"/features/{kind}/{i}/id", f"duplicate {kind} feature {feature_id!r}")
                    target.add(feature_id)

                    if prefix == "ktrf" and feature_id not in CORE_FEATURES:
                        self.error("KTRF1202", f"/features/{kind}/{i}/id", f"unknown core feature {feature_id!r}")

                    if kind == "required" and self.enforce_capabilities:
                        available = self.supported_features.get(feature_id)
                        if available is None:
                            self.error("KTRF1203", f"/features/{kind}/{i}", f"required feature {feature_id!r} is unsupported")
                        elif isinstance(version, str) and not self._version_satisfies(available, version):
                            self.error(
                                "KTRF1204",
                                f"/features/{kind}/{i}/version",
                                f"required {feature_id}@{version} is not satisfied by {available}",
                            )

        overlap = required_ids & optional_ids
        for feature_id in sorted(overlap):
            self.error("KTRF1205", "/features", f"feature {feature_id!r} cannot be both required and optional")

    def _ref(self, collection: str, value: Any, path: str) -> Mapping[str, Any] | None:
        if not isinstance(value, str):
            return None
        found = self.indices.get(collection, {}).get(value)
        if found is None:
            self.error("KTRF1301", path, f"unknown {collection} reference {value!r}")
        return found

    def _validate_entry_points(self) -> None:
        for i, row in enumerate(self._records("entry_points")):
            self._ref("nodes", row.get("node"), f"/entry_points/{i}/node")
            trigger = row.get("trigger")
            if trigger is not None:
                self._check_qname(trigger, f"/entry_points/{i}/trigger", known_core=CORE_TRIGGERS)

    def _validate_variables(self) -> None:
        for i, row in enumerate(self._records("variables")):
            type_name = row.get("type")
            scope = row.get("scope")
            self._check_qname(type_name, f"/variables/{i}/type", known_core=CORE_TYPES)
            self._check_qname(scope, f"/variables/{i}/scope", known_core=CORE_SCOPES)
            if isinstance(type_name, str) and type_name in CORE_TYPES:
                self._check_literal_type(row.get("default"), type_name, f"/variables/{i}/default", code="KTRF1401")

    def _validate_resources(self) -> None:
        for i, row in enumerate(self._records("resource_locators")):
            self._check_qname(row.get("scheme"), f"/resource_locators/{i}/scheme", known_core=CORE_RESOURCE_SCHEMES)

    def _validate_hooks(self) -> None:
        for i, row in enumerate(self._records("external_hooks")):
            self._check_qname(row.get("symbol"), f"/external_hooks/{i}/symbol")

    def _validate_endings(self) -> None:
        # Structural schema owns scalar shape. Semantic identity is handled by
        # the collection index and register-ending effect checks.
        return

    def _validate_value_ref(self, value: Any, path: str) -> str | None:
        if not isinstance(value, Mapping):
            self.error("KTRF1501", path, "value reference must be an object")
            return None
        kind = value.get("kind")
        if kind == "literal":
            return self._infer_literal_type(value.get("value"))
        if kind == "variable":
            record = self._ref("variables", value.get("ref"), f"{path}/ref")
            return record.get("type") if record else None
        if kind == "expression":
            record = self._ref("expressions", value.get("ref"), f"{path}/ref")
            return record.get("result_type") if record else None
        if kind == "entity":
            entity = value.get("entity")
            mapping = {
                "ending": "endings",
                "node": "nodes",
                "external_hook": "external_hooks",
                "resource_locator": "resource_locators",
                "variable": "variables",
            }
            collection = mapping.get(entity)
            if collection is None:
                self.error("KTRF1502", f"{path}/entity", f"unknown entity class {entity!r}")
            else:
                self._ref(collection, value.get("ref"), f"{path}/ref")
            return f"entity:{entity}" if isinstance(entity, str) else None
        self.error("KTRF1503", f"{path}/kind", f"unknown value reference kind {kind!r}")
        return None

    def _validate_expressions(self) -> None:
        for i, row in enumerate(self._records("expressions")):
            op = row.get("op")
            prefix = self._check_qname(op, f"/expressions/{i}/op", known_core=CORE_EXPRESSION_OPS)
            result_type = row.get("result_type")
            if result_type is not None:
                self._check_qname(result_type, f"/expressions/{i}/result_type", known_core=CORE_TYPES)
            args = row.get("args", [])
            if not isinstance(args, list):
                continue
            arg_types = [self._validate_value_ref(arg, f"/expressions/{i}/args/{j}") for j, arg in enumerate(args)]

            if prefix == "ktrf" and isinstance(op, str):
                self._validate_core_expression_contract(i, op, args, arg_types, result_type)
            elif prefix and not self._namespace_has_required_feature(prefix):
                self.error(
                    "KTRF1504",
                    f"/expressions/{i}/op",
                    f"profile expression operator {op!r} requires a declared required feature in namespace {prefix!r}",
                )

    def _validate_core_expression_contract(
        self,
        index: int,
        op: str,
        args: Sequence[Any],
        arg_types: Sequence[str | None],
        result_type: Any,
    ) -> None:
        path = f"/expressions/{index}"
        bool_ops = {"ktrf:eq", "ktrf:ne", "ktrf:lt", "ktrf:le", "ktrf:gt", "ktrf:ge", "ktrf:and", "ktrf:or", "ktrf:not"}
        if op in {"ktrf:eq", "ktrf:ne", "ktrf:lt", "ktrf:le", "ktrf:gt", "ktrf:ge"} and len(args) != 2:
            self.error("KTRF1510", f"{path}/args", f"{op} requires exactly 2 arguments")
        if op in {"ktrf:and", "ktrf:or"} and len(args) < 2:
            self.error("KTRF1511", f"{path}/args", f"{op} requires at least 2 arguments")
        if op == "ktrf:not" and len(args) != 1:
            self.error("KTRF1512", f"{path}/args", "ktrf:not requires exactly 1 argument")
        if op in {"ktrf:const", "ktrf:var"} and len(args) != 1:
            self.error("KTRF1513", f"{path}/args", f"{op} requires exactly 1 argument")
        if op in bool_ops and result_type != "ktrf:bool":
            self.error("KTRF1514", f"{path}/result_type", f"{op} must declare result_type ktrf:bool")
        if op in {"ktrf:and", "ktrf:or", "ktrf:not"}:
            for j, arg_type in enumerate(arg_types):
                if arg_type is not None and arg_type != "ktrf:bool":
                    self.error("KTRF1515", f"{path}/args/{j}", f"{op} requires boolean arguments")
        if op in {"ktrf:eq", "ktrf:ne"} and len(arg_types) == 2:
            left, right = arg_types
            if left and right and not self._types_comparable(left, right):
                self.error("KTRF1516", f"{path}/args", f"incompatible equality operands {left!r} and {right!r}")
        if op in {"ktrf:lt", "ktrf:le", "ktrf:gt", "ktrf:ge"} and len(arg_types) == 2:
            for j, arg_type in enumerate(arg_types):
                if arg_type and not self._is_numeric_type(arg_type):
                    self.error("KTRF1517", f"{path}/args/{j}", f"ordered comparison requires numeric operands, got {arg_type!r}")

    def _validate_expression_cycles(self) -> None:
        graph: dict[str, list[str]] = {}
        for expr_id, row in self.indices.get("expressions", {}).items():
            refs: list[str] = []
            for arg in row.get("args", []):
                if isinstance(arg, Mapping) and arg.get("kind") == "expression" and isinstance(arg.get("ref"), str):
                    refs.append(arg["ref"])
            graph[expr_id] = refs

        visiting: set[str] = set()
        visited: set[str] = set()
        stack: list[str] = []

        def dfs(node: str) -> None:
            if node in visited:
                return
            if node in visiting:
                try:
                    start = stack.index(node)
                    cycle = stack[start:] + [node]
                except ValueError:
                    cycle = [node, node]
                self.error("KTRF1520", "/expressions", "expression cycle: " + " -> ".join(cycle))
                return
            visiting.add(node)
            stack.append(node)
            for nxt in graph.get(node, []):
                if nxt in graph:
                    dfs(nxt)
            stack.pop()
            visiting.remove(node)
            visited.add(node)

        for expr_id in graph:
            dfs(expr_id)

    def _validate_effects(self) -> None:
        for i, row in enumerate(self._records("effects")):
            op = row.get("op")
            prefix = self._check_qname(op, f"/effects/{i}/op", known_core=CORE_EFFECT_OPS)
            args = row.get("args")
            if not isinstance(args, Mapping):
                continue
            if prefix == "ktrf" and isinstance(op, str):
                self._validate_core_effect_contract(i, op, args)
            elif prefix and not self._namespace_has_required_feature(prefix):
                self.error(
                    "KTRF1601",
                    f"/effects/{i}/op",
                    f"profile effect operator {op!r} requires a declared required feature in namespace {prefix!r}",
                )

    def _validate_core_effect_contract(self, index: int, op: str, args: Mapping[str, Any]) -> None:
        path = f"/effects/{index}/args"
        if op == "ktrf:set":
            target = self._ref("variables", args.get("target"), f"{path}/target")
            value_type = self._validate_value_ref(args.get("value"), f"{path}/value")
            if target and value_type and not self._type_assignable(target.get("type"), value_type, args.get("value")):
                self.error("KTRF1610", f"{path}/value", f"cannot assign {value_type!r} to {target.get('type')!r}")
        elif op == "ktrf:copy":
            target = self._ref("variables", args.get("target"), f"{path}/target")
            source = self._ref("variables", args.get("source"), f"{path}/source")
            if target and source and not self._types_assignable_names(target.get("type"), source.get("type")):
                self.error("KTRF1611", path, f"cannot copy {source.get('type')!r} into {target.get('type')!r}")
        elif op == "ktrf:add":
            target = self._ref("variables", args.get("target"), f"{path}/target")
            value_type = self._validate_value_ref(args.get("value"), f"{path}/value")
            if target and not self._is_numeric_type(target.get("type")):
                self.error("KTRF1612", f"{path}/target", "ktrf:add target must be numeric")
            if value_type and not self._is_numeric_type(value_type):
                self.error("KTRF1613", f"{path}/value", "ktrf:add value must be numeric")
        elif op == "ktrf:register-ending":
            self._ref("endings", args.get("ending"), f"{path}/ending")
        elif op == "ktrf:call-hook":
            self._ref("external_hooks", args.get("hook"), f"{path}/hook")
            arguments = args.get("arguments", [])
            if not isinstance(arguments, list):
                self.error("KTRF1614", f"{path}/arguments", "call-hook arguments must be an array")
            else:
                for j, value in enumerate(arguments):
                    self._validate_value_ref(value, f"{path}/arguments/{j}")

    def _validate_choices(self) -> None:
        for i, row in enumerate(self._records("choices")):
            self._ref("nodes", row.get("node"), f"/choices/{i}/node")
            result_var = self._ref("variables", row.get("result_variable"), f"/choices/{i}/result_variable")
            self._check_qname(row.get("routing_policy"), f"/choices/{i}/routing_policy", known_core=CORE_ROUTING_POLICIES)
            local_ids: set[str] = set()
            values: list[Any] = []
            for j, option in enumerate(row.get("options", [])):
                if not isinstance(option, Mapping):
                    continue
                local_id = option.get("id")
                if isinstance(local_id, str):
                    if local_id in local_ids:
                        self.error("KTRF1701", f"/choices/{i}/options/{j}/id", f"duplicate local option ID {local_id!r}")
                    local_ids.add(local_id)
                value = option.get("value")
                if value in values:
                    self.error("KTRF1702", f"/choices/{i}/options/{j}/value", f"duplicate choice result value {value!r}")
                values.append(value)
                if result_var:
                    self._check_literal_type(value, result_var.get("type"), f"/choices/{i}/options/{j}/value", code="KTRF1703")
                for k, effect_id in enumerate(option.get("effects", [])):
                    self._ref("effects", effect_id, f"/choices/{i}/options/{j}/effects/{k}")
            timeout = row.get("timeout")
            if isinstance(timeout, Mapping):
                value = timeout.get("value")
                if result_var:
                    self._check_literal_type(value, result_var.get("type"), f"/choices/{i}/timeout/value", code="KTRF1704")
                for k, effect_id in enumerate(timeout.get("effects", [])):
                    self._ref("effects", effect_id, f"/choices/{i}/timeout/effects/{k}")

    def _validate_nodes(self) -> None:
        for i, row in enumerate(self._records("nodes")):
            self._check_qname(row.get("kind"), f"/nodes/{i}/kind", known_core=CORE_NODE_KINDS)
            for j, resource_id in enumerate(row.get("resources", [])):
                self._ref("resource_locators", resource_id, f"/nodes/{i}/resources/{j}")

    def _validate_transitions(self) -> None:
        priority_keys: dict[tuple[str, str, int], str] = {}
        for i, row in enumerate(self._records("transitions")):
            source = row.get("source")
            self._ref("nodes", source, f"/transitions/{i}/source")
            terminal = row.get("terminal") is True
            destination = row.get("destination")
            if terminal:
                if destination is not None:
                    self.error("KTRF1801", f"/transitions/{i}/destination", "terminal transition must not have a destination")
            else:
                if destination is None:
                    self.error("KTRF1802", f"/transitions/{i}", "non-terminal transition requires a destination")
                else:
                    self._ref("nodes", destination, f"/transitions/{i}/destination")

            predicate = row.get("predicate")
            if predicate is not None:
                expr = self._ref("expressions", predicate, f"/transitions/{i}/predicate")
                if expr and expr.get("result_type") != "ktrf:bool":
                    self.error("KTRF1803", f"/transitions/{i}/predicate", "transition predicate must yield ktrf:bool")

            for j, effect_id in enumerate(row.get("effects", [])):
                self._ref("effects", effect_id, f"/transitions/{i}/effects/{j}")

            triggers = row.get("triggers")
            if triggers is None:
                triggers = ["ktrf:next"]
            if not isinstance(triggers, list):
                continue
            if len(triggers) != len(set(x for x in triggers if isinstance(x, str))):
                self.error("KTRF1804", f"/transitions/{i}/triggers", "transition contains duplicate triggers")
            for j, trigger in enumerate(triggers):
                self._check_qname(trigger, f"/transitions/{i}/triggers/{j}", known_core=CORE_TRIGGERS)
                if isinstance(source, str) and isinstance(trigger, str) and isinstance(row.get("priority"), int):
                    key = (source, trigger, row["priority"])
                    previous = priority_keys.get(key)
                    if previous is not None:
                        self.error(
                            "KTRF1805",
                            f"/transitions/{i}/priority",
                            f"priority collision for source={source!r}, trigger={trigger!r}, priority={row['priority']} with {previous!r}",
                        )
                    else:
                        priority_keys[key] = str(row.get("id"))

    def _validate_extensions(self) -> None:
        all_required = {row.get("id") for row in self._feature_rows("required")}

        def validate_blocks(blocks: Any, base: str) -> None:
            if not isinstance(blocks, list):
                return
            for i, ext in enumerate(blocks):
                if not isinstance(ext, Mapping):
                    continue
                namespace = ext.get("namespace")
                if isinstance(namespace, str) and namespace not in self.namespaces:
                    self.error("KTRF1901", f"{base}/{i}/namespace", f"extension namespace {namespace!r} is not declared")
                required_feature = ext.get("required_feature")
                if required_feature is not None:
                    self._check_qname(required_feature, f"{base}/{i}/required_feature")
                    if required_feature not in all_required:
                        self.error(
                            "KTRF1902",
                            f"{base}/{i}/required_feature",
                            f"extension required_feature {required_feature!r} must appear in features.required",
                        )

        validate_blocks(self.doc.get("extensions"), "/extensions")
        for collection in COLLECTIONS:
            for i, row in enumerate(self._records(collection)):
                validate_blocks(row.get("extensions"), f"/{collection}/{i}/extensions")

    def _namespace_has_required_feature(self, prefix: str) -> bool:
        needle = prefix + ":"
        return any(
            isinstance(row.get("id"), str) and row["id"].startswith(needle)
            for row in self._feature_rows("required")
        )

    @staticmethod
    def _infer_literal_type(value: Any) -> str | None:
        if isinstance(value, bool):
            return "ktrf:bool"
        if isinstance(value, int):
            return "literal:int"
        if isinstance(value, float):
            return "literal:float"
        if isinstance(value, str):
            return "ktrf:string"
        if value is None:
            return "literal:null"
        return None

    @staticmethod
    def _is_numeric_type(type_name: Any) -> bool:
        return type_name in {
            "ktrf:int32",
            "ktrf:uint32",
            "ktrf:float32",
            "ktrf:float64",
            "literal:int",
            "literal:float",
        }

    @classmethod
    def _types_comparable(cls, left: str, right: str) -> bool:
        if left == right:
            return True
        return cls._is_numeric_type(left) and cls._is_numeric_type(right)

    @classmethod
    def _types_assignable_names(cls, target: Any, source: Any) -> bool:
        if target == source:
            return True
        if target in {"ktrf:float32", "ktrf:float64"} and cls._is_numeric_type(source):
            return True
        return False

    def _type_assignable(self, target: Any, source: str, value_ref: Any) -> bool:
        if source == "literal:int" and isinstance(value_ref, Mapping):
            return self._literal_fits_integer(value_ref.get("value"), target) or target in {"ktrf:float32", "ktrf:float64"}
        if source == "literal:float":
            return target in {"ktrf:float32", "ktrf:float64"}
        return self._types_assignable_names(target, source)

    @staticmethod
    def _literal_fits_integer(value: Any, target: Any) -> bool:
        if isinstance(value, bool) or not isinstance(value, int):
            return False
        if target == "ktrf:int32":
            return -(2**31) <= value <= 2**31 - 1
        if target == "ktrf:uint32":
            return 0 <= value <= 2**32 - 1
        return False

    def _check_literal_type(self, value: Any, target_type: Any, path: str, *, code: str) -> None:
        if target_type not in CORE_TYPES:
            return
        ok = False
        if target_type == "ktrf:bool":
            ok = isinstance(value, bool)
        elif target_type in {"ktrf:int32", "ktrf:uint32"}:
            ok = self._literal_fits_integer(value, target_type)
        elif target_type in {"ktrf:float32", "ktrf:float64"}:
            ok = isinstance(value, (int, float)) and not isinstance(value, bool)
        elif target_type in {"ktrf:string", "ktrf:bytes"}:
            ok = isinstance(value, str)
        if not ok:
            self.error(code, path, f"value {value!r} is incompatible with {target_type}")


def schema_validate(document: Any, schema_path: Path) -> list[Diagnostic]:
    try:
        import jsonschema
    except ImportError as exc:
        raise RuntimeError(
            "jsonschema is required for structural validation; install it or use --skip-schema"
        ) from exc

    schema = json.loads(schema_path.read_text(encoding="utf-8-sig"))
    validator_cls = jsonschema.validators.validator_for(schema)
    validator_cls.check_schema(schema)
    validator = validator_cls(schema)
    diagnostics: list[Diagnostic] = []
    for error in sorted(validator.iter_errors(document), key=lambda e: list(e.absolute_path)):
        pointer = "/" + "/".join(str(x) for x in error.absolute_path)
        diagnostics.append(Diagnostic("error", "KTRF0001", pointer, error.message))
    return diagnostics


def parse_supported_feature(value: str) -> tuple[str, str]:
    if "@" not in value:
        raise argparse.ArgumentTypeError("feature must use ID@MAJOR.MINOR.PATCH")
    feature_id, version = value.rsplit("@", 1)
    if not QNAME_RE.match(feature_id) or not SEMVER_RE.match(version):
        raise argparse.ArgumentTypeError("feature must use qualified-name@semver")
    return feature_id, version


def render_text(path: Path, diagnostics: Sequence[Diagnostic]) -> None:
    errors = [d for d in diagnostics if d.severity == "error"]
    warnings = [d for d in diagnostics if d.severity == "warning"]
    if diagnostics:
        for d in diagnostics:
            print(f"{d.severity.upper()} {d.code} {d.path}: {d.message}")
    if errors:
        print(f"KTRF IR INVALID: {len(errors)} error(s), {len(warnings)} warning(s) — {path}")
    else:
        print(f"KTRF IR PASS: 0 errors, {len(warnings)} warning(s) — {path}")


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Validate KTRF Canonical Routing IR v0.1")
    parser.add_argument("input", type=Path, help="Routing IR JSON document")
    parser.add_argument("--schema", type=Path, default=None, help="JSON Schema path")
    parser.add_argument("--skip-schema", action="store_true", help="skip JSON Schema validation")
    parser.add_argument("--json", action="store_true", dest="json_output", help="emit machine-readable diagnostics")
    parser.add_argument(
        "--enforce-capabilities",
        action="store_true",
        help="reject required features not present in the validator capability set",
    )
    parser.add_argument(
        "--supported-feature",
        action="append",
        default=[],
        type=parse_supported_feature,
        metavar="ID@VERSION",
        help="add a supported feature for --enforce-capabilities; repeatable",
    )
    args = parser.parse_args(argv)

    try:
        input_path = args.input.resolve()
        document = json.loads(input_path.read_text(encoding="utf-8-sig"))
        diagnostics: list[Diagnostic] = []

        if not args.skip_schema:
            schema_path = args.schema
            if schema_path is None:
                repo_root = Path(__file__).resolve().parents[2]
                schema_path = repo_root / "schemas" / "ktrf-routing-ir.schema.json"
            diagnostics.extend(schema_validate(document, schema_path.resolve()))

        # Semantic checks are useful even when schema errors exist, provided the
        # top level is an object. The validator is intentionally defensive.
        if isinstance(document, Mapping):
            supported = dict(args.supported_feature)
            diagnostics.extend(
                Validator(
                    document,
                    enforce_capabilities=args.enforce_capabilities,
                    supported_features=supported,
                ).validate()
            )
        else:
            diagnostics.append(Diagnostic("error", "KTRF0002", "/", "top-level JSON value must be an object"))

        if args.json_output:
            errors = sum(d.severity == "error" for d in diagnostics)
            warnings = sum(d.severity == "warning" for d in diagnostics)
            print(
                json.dumps(
                    {
                        "format": "ktrf-validation-report-v0.1",
                        "input": str(input_path),
                        "valid": errors == 0,
                        "errors": errors,
                        "warnings": warnings,
                        "diagnostics": [d.as_dict() for d in diagnostics],
                    },
                    indent=2,
                    ensure_ascii=False,
                )
            )
        else:
            render_text(input_path, diagnostics)

        return 0 if not any(d.severity == "error" for d in diagnostics) else 1
    except (OSError, json.JSONDecodeError, RuntimeError, ValueError) as exc:
        if args.json_output:
            print(
                json.dumps(
                    {
                        "format": "ktrf-validation-report-v0.1",
                        "valid": False,
                        "errors": 1,
                        "warnings": 0,
                        "diagnostics": [
                            {
                                "severity": "error",
                                "code": "KTRF0000",
                                "path": "/",
                                "message": str(exc),
                            }
                        ],
                    },
                    indent=2,
                    ensure_ascii=False,
                )
            )
        else:
            print(f"ERROR KTRF0000 /: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
