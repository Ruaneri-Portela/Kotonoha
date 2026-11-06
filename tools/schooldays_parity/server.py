"""Small stdio MCP adapter; all game transport stays in local controllers."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys
import time

from .compare import compare_frames, frame_metrics
from .controllers import KotonohaController, OriginalController, SERVER_VERSION
from .parity import ParityController


def schema(properties: dict | None = None, required: list[str] | None = None) -> dict:
    return {"type": "object", "properties": properties or {},
            "required": required or [], "additionalProperties": False}


NUM = {"type": "number", "minimum": 0, "maximum": 1}
LABEL = {"type": "string", "maxLength": 64}
KEY = {"type": "string", "maxLength": 32}
CLICK = schema({"x": NUM, "y": NUM}, ["x", "y"])
NO_ARGS = schema()
RECORD = schema({"durationMs": {"type": "integer", "minimum": 1, "maximum": 10000},
                 "fpsTarget": {"type": "integer", "minimum": 1, "maximum": 60},
                 "label": LABEL,
                 "blackLuma": {"type": "number", "minimum": 0, "maximum": 255},
                 "maxSyncMs": {"type": "number", "minimum": 0, "maximum": 1000}})


def definitions(mode: str) -> dict[str, dict]:
    if mode == "kotonoha":
        tools = {name: NO_ARGS for name in (
            "health", "game_start", "game_stop", "game_status", "get_window", "get_frame",
            "get_scene", "get_current_script", "get_current_node",
            "get_timeline_time", "get_choices", "get_media_state",
            "get_audio_state", "get_background_state", "get_abc_state",
            "get_menu_state", "get_system_ui_state", "get_save_load_state",
            "get_replay_state", "get_playdata_state", "get_display_state",
            "get_transition_state", "get_logs", "clear_logs",
            "pause_runtime", "resume_runtime", "step_frame")}
        tools.update({"click": CLICK, "mouse_move": CLICK, "mouse_down": CLICK,
                      "mouse_up": CLICK, "key_down": schema({"key": KEY}, ["key"]),
                      "key_up": schema({"key": KEY}, ["key"]),
                      "choose": schema({"index": {"type": "integer", "minimum": 0}}, ["index"]),
                      "wait_frames": schema({"frames": {"type": "integer", "minimum": 1, "maximum": 10000}}, ["frames"]),
                      "save_screenshot": schema({"label": LABEL}),
                      "capture_frame": schema({"label": LABEL})})
        return tools
    if mode == "original":
        return {"health": NO_ARGS, "original_start": NO_ARGS, "original_stop": NO_ARGS,
                "original_status": NO_ARGS, "original_get_window": NO_ARGS,
                "original_capture_frame": schema({"label": LABEL}),
                "original_click": CLICK, "original_mouse_move": CLICK,
                "original_key": schema({"key": KEY}, ["key"]),
                "original_wait": schema({"ms": {"type": "integer", "minimum": 0,
                                                  "maximum": 60000}}, ["ms"]),
                "original_get_logs": NO_ARGS}
    return {"health": NO_ARGS, "start_both": NO_ARGS, "stop_both": NO_ARGS,
            "capture_both": schema({"label": LABEL, "heatmap": {"type": "boolean"},
                                     "black_luma": {"type": "number", "minimum": 0, "maximum": 255},
                                     "maxSyncMs": {"type": "number", "minimum": 0, "maximum": 1000}}),
            "click_both": CLICK, "click_both_normalized": CLICK,
            "key_both": schema({"key": KEY}, ["key"]),
            "wait_both": schema({"ms": {"type": "integer", "minimum": 0,
                                          "maximum": 60000}}, ["ms"]),
            "compare_frames": schema({"original": {"type": "string"},
                                      "kotonoha": {"type": "string"},
                                      "heatmap": {"type": "boolean"}},
                                     ["original", "kotonoha"]),
            "compare_audio_metrics": NO_ARGS,
            "compare_scene_state": NO_ARGS,
            "record_transition": RECORD,
            "compare_last_transition": schema({"blackRatio": NUM,
                                                "normalRatio": NUM,
                                                "normalWindow": {"type": "integer", "minimum": 1, "maximum": 60},
                                                "maxSyncMs": {"type": "number", "minimum": 0, "maximum": 1000},
                                                "maxGapMs": {"type": "number", "exclusiveMinimum": 0, "maximum": 10000},
                                                "heatmaps": {"type": "boolean"}}),
            "analyze_transition": schema({"side": {"type": "string", "enum": ["original", "kotonoha"]},
                                           "changeThreshold": {"type": "number", "minimum": 0, "maximum": 255},
                                           "stableFrames": {"type": "integer", "minimum": 1, "maximum": 60},
                                           "blackRatio": NUM}),
            "compare_choice_transition": schema({"x": NUM, "y": NUM,
                                                   "durationMs": {"type": "integer", "minimum": 1, "maximum": 10000},
                                                   "fpsTarget": {"type": "integer", "minimum": 1, "maximum": 60},
                                                   "label": LABEL}, ["x", "y"]),
            "run_input_sequence": schema({"actions": {"type": "array", "maxItems": 1000,
                                                      "items": {"type": "object"}}}, ["actions"]),
            "get_parity_report": NO_ARGS}


class Server:
    def __init__(self, mode: str, port: int):
        self.mode = mode
        self.tools = definitions(mode)
        self.controller = (KotonohaController(port) if mode == "kotonoha"
                           else OriginalController() if mode == "original"
                           else ParityController(port))

    def invoke(self, name: str, args: dict) -> dict:
        if name not in self.tools:
            raise ValueError(f"Unknown tool: {name}")
        c = self.controller
        if name == "health": return c.health()
        if self.mode == "kotonoha":
            if name == "game_start": return c.start()
            if name == "game_stop": return c.stop()
            if name == "game_status": return c.status()
            if name in ("get_frame", "capture_frame", "save_screenshot"):
                return c.capture(args.get("label", "kotonoha"))
            if name == "click": return c.click(float(args["x"]), float(args["y"]))
            if name == "mouse_move": return c.move(float(args["x"]), float(args["y"]))
            if name in ("mouse_down", "mouse_up"):
                if not 0 <= float(args["x"]) <= 1 or not 0 <= float(args["y"]) <= 1:
                    raise ValueError("Coordinates must be normalized to 0..1")
                window = c.status()["window"]
                return c.call(name, x=round(float(args["x"]) * window["width"]),
                              y=round(float(args["y"]) * window["height"]))
            if name in ("key_down", "key_up"):
                return c.call(name, key=args["key"])
            if name == "wait_frames":
                start = c.status()["frame"]
                target = start + args["frames"]
                deadline = time.monotonic() + 60
                while time.monotonic() < deadline:
                    state = c.status()
                    if state["frame"] >= target: return state
                    time.sleep(0.005)
                raise TimeoutError("wait_frames timed out")
            if name == "step_frame":
                c.call("pause_runtime")
                before = c.status()["update"]
                c.call("step_frame")
                deadline = time.monotonic() + 5
                while time.monotonic() < deadline:
                    state = c.status()
                    if state["update"] > before: return state
                    time.sleep(0.005)
                raise TimeoutError("step_frame timed out")
            return c.call(name, **args)
        if self.mode == "original":
            if name == "original_start": return c.start()
            if name == "original_stop": return c.stop()
            if name in ("original_status", "original_get_window"): return c.status()
            if name == "original_capture_frame": return c.capture(args.get("label", "original"))
            if name == "original_click": return c.click(float(args["x"]), float(args["y"]))
            if name == "original_mouse_move": return c.move(float(args["x"]), float(args["y"]))
            if name == "original_key": return c.key(args["key"])
            if name == "original_wait":
                time.sleep(args["ms"] / 1000)
                return {"waited_ms": args["ms"]}
            return {"events": c.actions, "capability": "external visual observation"}
        if name == "start_both": return c.start_both()
        if name == "stop_both": return c.stop_both()
        if name in ("click_both", "click_both_normalized"):
            return c.click_both(float(args["x"]), float(args["y"]))
        if name == "key_both": return c.key_both(args["key"])
        if name == "wait_both":
            time.sleep(args["ms"] / 1000)
            return {"waited_ms": args["ms"]}
        if name == "capture_both":
            return c.capture_both(args.get("label", "pair"),
                                  heatmap=args.get("heatmap", False),
                                  black_luma=args.get("black_luma", 12.0),
                                  max_sync_ms=args.get("maxSyncMs", 50.0))
        if name == "record_transition":
            return c.record_transition(args.get("durationMs", 1000),
                                       args.get("fpsTarget", 30),
                                       args.get("label", "transition"),
                                       black_luma=args.get("blackLuma", 12.0),
                                       max_sync_ms=args.get("maxSyncMs", 50.0))
        if name == "compare_last_transition":
            return c.compare_last_transition(black_ratio=args.get("blackRatio", 0.95),
                                             normal_ratio=args.get("normalRatio", 0.7),
                                             normal_window=args.get("normalWindow", 4),
                                             max_sync_ms=args.get("maxSyncMs"),
                                             max_gap_ms=args.get("maxGapMs"),
                                             heatmaps=args.get("heatmaps", False))
        if name == "analyze_transition":
            return c.analyze_transition(args.get("side", "original"),
                                        change_threshold=args.get("changeThreshold", 4.0),
                                        stable_frames=args.get("stableFrames", 2),
                                        black_ratio=args.get("blackRatio", 0.95))
        if name == "compare_choice_transition":
            return c.compare_choice_transition(float(args["x"]), float(args["y"]),
                                               duration_ms=args.get("durationMs", 1000),
                                               fps_target=args.get("fpsTarget", 30),
                                               label=args.get("label", "choice"))
        if name == "compare_frames":
            # Only files previously produced under the parity capture root.
            from .controllers import OUTPUT
            left = Path(args["original"]).resolve()
            right = Path(args["kotonoha"]).resolve()
            root = OUTPUT.resolve()
            if root not in left.parents or root not in right.parents:
                raise ValueError("Frame paths must be parity capture artifacts")
            return compare_frames(left, right, heatmap=args.get("heatmap", False))
        if name == "compare_scene_state":
            return {"kotonoha": c.kotonoha.status(),
                    "original": c.original.status(),
                    "original_internal_state_available": False}
        if name == "compare_audio_metrics":
            return {"kotonoha": c.kotonoha.call("get_audio_state"),
                    "original": {"available": False,
                                 "reason": "Per-process loopback adapter is not yet implemented"}}
        if name == "run_input_sequence": return c.run_input_sequence(args["actions"])
        return c.get_parity_report()

    def dispatch(self, request: dict) -> dict | None:
        method = request.get("method", "")
        if "id" not in request:
            return None
        rid = request["id"]
        try:
            if method == "initialize":
                result = {"protocolVersion": request.get("params", {}).get("protocolVersion", "2024-11-05"),
                          "capabilities": {"tools": {}},
                          "serverInfo": {"name": f"schooldays-{self.mode}", "version": SERVER_VERSION}}
            elif method == "ping": result = {}
            elif method == "tools/list":
                result = {"tools": [{"name": name, "description": f"{self.mode}: {name}",
                                      "inputSchema": tool_schema}
                                     for name, tool_schema in self.tools.items()]}
            elif method == "tools/call":
                params = request.get("params", {})
                value = self.invoke(params["name"], params.get("arguments", {}))
                result = {"content": [{"type": "text", "text": json.dumps(value, ensure_ascii=False)}]}
            else:
                raise ValueError(f"Unsupported MCP method: {method}")
            return {"jsonrpc": "2.0", "id": rid, "result": result}
        except Exception as error:
            return {"jsonrpc": "2.0", "id": rid,
                    "error": {"code": -32000, "message": str(error)}}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mode", choices=("kotonoha", "original", "parity"), required=True)
    parser.add_argument("--port", type=int, default=47321)
    args = parser.parse_args()
    server = Server(args.mode, args.port)
    input_stream = sys.stdin.buffer
    output_stream = sys.stdout.buffer
    while True:
        line = input_stream.readline()
        if not line: break
        if line.lower().startswith(b"content-length:"):
            length = int(line.split(b":", 1)[1].strip())
            while input_stream.readline().strip(): pass
            payload = input_stream.read(length)
            framed = True
        else:
            payload = line
            framed = False
        try:
            response = server.dispatch(json.loads(payload))
        except Exception as error:
            print(f"MCP parse error: {error}", file=sys.stderr)
            continue
        if response is None: continue
        encoded = json.dumps(response, ensure_ascii=False, separators=(",", ":")).encode() + (b"" if framed else b"\n")
        if framed:
            output_stream.write(f"Content-Length: {len(encoded)}\r\n\r\n".encode())
        output_stream.write(encoded)
        output_stream.flush()


if __name__ == "__main__":
    main()
