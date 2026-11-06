"""Mirrored input, bounded transition capture, and evidence-backed comparison."""
from __future__ import annotations

from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
import json
from pathlib import Path
import time
from typing import Callable

from .compare import compare_frames, detect_black_window, frame_metrics, temporal_delta
from .controllers import KotonohaController, OriginalController, OUTPUT, _label, SERVER_VERSION


class ParityController:
    def __init__(self, port: int = 47321):
        self.kotonoha = KotonohaController(port)
        self.original = OriginalController()
        self.action_id = 0
        self.actions: list[dict] = []
        self.history: list[dict] = []
        self.report: list[dict] = []
        self.last_transition: dict | None = None

    def health(self) -> dict:
        return {"serverVersion": SERVER_VERSION, "kotonoha": self.kotonoha.health(),
                "original": self.original.health(), "captureRoot": str(OUTPUT),
                "lastActionId": self.action_id}

    def start_both(self) -> dict:
        original_was_running = self.original.process is not None and self.original.process.poll() is None
        original = self.original.start()
        try:
            return {"original": original, "kotonoha": self.kotonoha.start()}
        except Exception:
            if not original_was_running:
                self.original.stop()
            raise

    def stop_both(self) -> dict:
        try:
            original = self.original.stop()
        finally:
            kotonoha = self.kotonoha.stop()
        return {"original": original, "kotonoha": kotonoha}

    def _mirror(self, kind: str, *values, wait_processed: bool = True) -> dict:
        self.action_id += 1
        action_id = self.action_id
        issued = time.monotonic_ns()
        def send_kotonoha() -> tuple[int, dict]:
            queued = time.monotonic_ns()
            result = (self.kotonoha.click(*values, action_id) if kind == "click"
                      else self.kotonoha.key(*values, action_id))
            return queued, result
        with ThreadPoolExecutor(max_workers=2) as pool:
            if kind == "click":
                original = pool.submit(self.original.click, *values, action_id)
            else:
                original = pool.submit(self.original.key, *values, action_id)
            kotonoha = pool.submit(send_kotonoha)
            original_result = original.result()
            kotonoha_sent, kotonoha_result = kotonoha.result()
        marker = {"actionId": action_id, "kind": kind, "arguments": values,
                  "issuedMonotonicNs": issued, "original": original_result["action"],
                  "kotonohaSendMonotonicNs": kotonoha_sent,
                  "inputSendDeltaMs": abs(original_result["action"]["sentMonotonicNs"] - kotonoha_sent) / 1e6,
                  "kotonohaQueuedAtFrame": kotonoha_result.get("frame"),
                  "kotonohaProcessed": None}
        if wait_processed:
            deadline = time.monotonic() + 1.5
            while time.monotonic() < deadline:
                state = self.kotonoha.status()
                if state.get("lastAction", {}).get("actionId") == action_id:
                    marker["kotonohaProcessed"] = state["lastAction"]
                    break
                time.sleep(0.005)
        self.actions.append(marker)
        return marker

    def click_both(self, x: float, y: float, *, wait_processed: bool = True) -> dict:
        if not 0 <= x <= 1 or not 0 <= y <= 1:
            raise ValueError("Coordinates must be normalized to 0..1")
        return self._mirror("click", x, y, wait_processed=wait_processed)

    def key_both(self, key: str) -> dict:
        return self._mirror("key", key)

    def _capture_pair(self, label: str, *, directory: Path = OUTPUT,
                      black_luma: float = 12.0, max_sync_ms: float = 50.0) -> dict:
        with ThreadPoolExecutor(max_workers=2) as pool:
            original = pool.submit(self.original.capture, label + "-original", directory / "original")
            kotonoha = pool.submit(self.kotonoha.capture, label + "-kotonoha", directory / "kotonoha")
            a, b = original.result(), kotonoha.result()
        if black_luma != 12.0:
            for capture in (a, b):
                capture["near_black_ratio"] = frame_metrics(
                    capture["path"], black_luma=black_luma)["near_black_ratio"]
        delta_ms = abs(a["capturedMonotonicNs"] - b["capturedMonotonicNs"]) / 1e6
        # The bridge timestamp crosses a socket/render-thread round trip. Its
        # midpoint is an estimate, so include half that interval in the gate.
        uncertainty_ms = b.get("timestampUncertaintyNs", 0) / 1e6
        upper_bound_ms = delta_ms + uncertainty_ms
        pair = {"label": label, "actionId": self.action_id,
                "original": a, "kotonoha": b, "captureDeltaMs": delta_ms,
                "captureUncertaintyMs": uncertainty_ms,
                "syncUpperBoundMs": upper_bound_ms,
                "synchronization": "SYNCHRONIZED" if upper_bound_ms <= max_sync_ms else "UNSYNCHRONIZED",
                "maxSyncMs": max_sync_ms, "blackLuma": black_luma}
        self.history.append(pair)
        return pair

    def capture_both(self, label: str = "pair", *, heatmap: bool = False,
                     black_luma: float = 12.0, max_sync_ms: float = 50.0) -> dict:
        pair = self._capture_pair(label, black_luma=black_luma, max_sync_ms=max_sync_ms)
        if pair["synchronization"] == "SYNCHRONIZED":
            pair["comparison"] = compare_frames(pair["original"]["path"],
                                                  pair["kotonoha"]["path"], heatmap=heatmap)
        else:
            pair["comparison"] = {"classification": "UNSYNCHRONIZED",
                                  "reason": "captureDeltaMs exceeds maxSyncMs"}
        self.report.append(pair)
        return pair

    def record_transition(self, duration_ms: int = 1000, fps_target: int = 30,
                          label: str = "transition", *, black_luma: float = 12.0,
                          max_sync_ms: float = 50.0, pre_pair: dict | None = None,
                          trigger: Callable[[], dict] | None = None) -> dict:
        if not 1 <= duration_ms <= 10000 or not 1 <= fps_target <= 60:
            raise ValueError("durationMs must be 1..10000 and fpsTarget 1..60")
        name = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ") + "-" + _label(label)
        directory = OUTPUT / name
        for child in ("original", "kotonoha", "heatmaps"):
            (directory / child).mkdir(parents=True, exist_ok=True)
        start = time.monotonic()
        period = 1.0 / fps_target
        pairs = []
        index = 0
        triggered_action = None
        while time.monotonic() - start <= duration_ms / 1000.0 and index <= 600:
            target = start + index * period
            if target > time.monotonic():
                time.sleep(target - time.monotonic())
            pairs.append(self._capture_pair(f"{index:04d}", directory=directory,
                                            black_luma=black_luma, max_sync_ms=max_sync_ms))
            if index == 0 and trigger is not None:
                triggered_action = trigger()
                start = time.monotonic() - period
            index += 1
        record = {"directory": str(directory), "actionId": self.action_id,
                  "durationMs": duration_ms, "fpsTarget": fps_target,
                  "blackLuma": black_luma, "maxSyncMs": max_sync_ms,
                  "triggeredAction": triggered_action,
                  "preFrame": pre_pair, "frames": pairs, "capturedFrames": len(pairs)}
        if len(pairs) > 1:
            gaps = [(pairs[i]["original"]["capturedMonotonicNs"] -
                     pairs[i - 1]["original"]["capturedMonotonicNs"]) / 1e6
                    for i in range(1, len(pairs))]
            elapsed_ms = sum(gaps)
            record["achievedOriginalFps"] = ((len(pairs) - 1) * 1000 / elapsed_ms
                                              if elapsed_ms > 0 else 0)
            record["maxOriginalGapMs"] = max(gaps)
        (directory / "metadata.json").write_text(
            json.dumps(record, ensure_ascii=False, indent=2), encoding="utf-8")
        self.last_transition = record
        return {"directory": str(directory), "actionId": self.action_id,
                "capturedFrames": len(pairs),
                "achievedOriginalFps": record.get("achievedOriginalFps"),
                "maxOriginalGapMs": record.get("maxOriginalGapMs"),
                "unsynchronizedFrames": sum(p["synchronization"] == "UNSYNCHRONIZED" for p in pairs)}

    def compare_last_transition(self, *, black_ratio: float = 0.95,
                                normal_ratio: float = 0.7, normal_window: int = 4,
                                max_sync_ms: float | None = None,
                                max_gap_ms: float | None = None,
                                heatmaps: bool = False) -> dict:
        if not 0 <= normal_ratio < black_ratio <= 1:
            raise ValueError("Require 0 <= normalRatio < blackRatio <= 1")
        if normal_window < 1:
            raise ValueError("normalWindow must be positive")
        record = self.last_transition
        if not record:
            raise RuntimeError("No transition has been recorded")
        pairs = record["frames"]
        tolerance = record["maxSyncMs"] if max_sync_ms is None else max_sync_ms
        gap_limit = (2000.0 / record["fpsTarget"] if max_gap_ms is None else max_gap_ms)
        if gap_limit <= 0:
            raise ValueError("maxGapMs must be positive")
        directory = Path(record["directory"])
        comparisons = []
        for index, pair in enumerate(pairs):
            for side in ("original", "kotonoha"):
                previous = pairs[index - 1][side]["path"] if index else (
                    record["preFrame"][side]["path"] if record["preFrame"] else None)
                pair[side + "DeltaFromPrevious"] = (
                    temporal_delta(previous, pair[side]["path"]) if previous else None)
                pair[side + "DeltaToNext"] = (
                    temporal_delta(pair[side]["path"], pairs[index + 1][side]["path"])
                    if index + 1 < len(pairs) else None)
            if pair["syncUpperBoundMs"] > tolerance:
                comparisons.append({"index": index, "classification": "UNSYNCHRONIZED",
                                    "captureDeltaMs": pair["captureDeltaMs"],
                                    "syncUpperBoundMs": pair["syncUpperBoundMs"]})
            else:
                result = compare_frames(pair["original"]["path"], pair["kotonoha"]["path"],
                                        heatmap=heatmaps,
                                        heatmap_path=directory / "heatmaps" / f"{index:04d}.png")
                comparisons.append({"index": index, "captureDeltaMs": pair["captureDeltaMs"],
                                    "comparison": result})
        flashes = detect_black_window(pairs, black_ratio=black_ratio,
                                      normal_ratio=normal_ratio,
                                      normal_window=normal_window,
                                      sync_tolerance_ms=tolerance,
                                      max_gap_ms=gap_limit)
        result = {"directory": str(directory), "actionId": record["actionId"],
                  "pairs": comparisons, "blackFlashEvents": flashes,
                  "thresholds": {"blackLuma": record["blackLuma"],
                                 "blackRatio": black_ratio, "normalRatio": normal_ratio,
                                 "normalWindow": normal_window, "maxSyncMs": tolerance,
                                 "maxGapMs": gap_limit},
                  "synchronizedPairs": sum(p["syncUpperBoundMs"] <= tolerance for p in pairs)}
        (directory / "comparison.json").write_text(
            json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
        self.report.append({"transition": result})
        return {"directory": str(directory), "comparisonPath": str(directory / "comparison.json"),
                "capturedFrames": len(pairs), "synchronizedPairs": result["synchronizedPairs"],
                "blackFlashEvents": flashes}

    def analyze_transition(self, side: str = "original", *, change_threshold: float = 4.0,
                           stable_frames: int = 2, black_ratio: float = 0.95) -> dict:
        if side not in ("original", "kotonoha"):
            raise ValueError("side must be original or kotonoha")
        if not self.last_transition:
            raise RuntimeError("No transition has been recorded")
        record = self.last_transition
        frames = record["frames"]
        if not frames:
            return {"preFrame": None, "transitionFrames": [], "blackFrames": [], "postFrame": None}
        baseline = record["preFrame"][side] if record["preFrame"] else frames[0][side]
        action = record.get("triggeredAction") or next(
            (item for item in reversed(self.actions)
             if item["actionId"] == record["actionId"]), None)
        sent_ns = (action["original"]["sentMonotonicNs"] if side == "original"
                   else action["kotonohaSendMonotonicNs"]) if action else 0
        changed = [i for i, pair in enumerate(frames)
                   if pair[side]["capturedMonotonicNs"] >= sent_ns and
                   temporal_delta(baseline["path"], pair[side]["path"]) >= change_threshold]
        first = changed[0] if changed else None
        black = [i for i, pair in enumerate(frames)
                 if pair[side]["capturedMonotonicNs"] >= sent_ns and
                 pair[side]["near_black_ratio"] >= black_ratio]
        post = None
        if first is not None:
            for i in range(first + stable_frames, len(frames)):
                if all(temporal_delta(frames[j - 1][side]["path"], frames[j][side]["path"]) < change_threshold
                       for j in range(i - stable_frames + 1, i + 1)):
                    post = i
                    break
        result = {"side": side, "preFrame": baseline,
                  "firstVisualChange": frames[first][side] if first is not None else None,
                  "transitionFrames": ([frames[i][side] for i in range(first, post or len(frames))]
                                       if first is not None else []),
                  "blackFrames": [frames[i][side] for i in black],
                  "postFrame": frames[post][side] if post is not None else None,
                  "changeThreshold": change_threshold, "stableFrames": stable_frames}
        if side == "original" and first is not None:
            for action in reversed(self.original.actions):
                if action["actionId"] == record["actionId"]:
                    action["firstVisualChangeNs"] = frames[first][side]["capturedMonotonicNs"]
                    break
            Path(record["directory"], "metadata.json").write_text(
                json.dumps(record, ensure_ascii=False, indent=2), encoding="utf-8")
        return result

    def compare_choice_transition(self, x: float, y: float, *,
                                  duration_ms: int = 1000, fps_target: int = 30,
                                  label: str = "choice") -> dict:
        state = self.kotonoha.status()
        if not state.get("choiceOpen") or not state.get("choices"):
            raise RuntimeError("Kotonoha DEV is not showing a selectable choice")
        if not self.original.status().get("running"):
            raise RuntimeError("Original window is unavailable; confirm its choice visually")
        pre = self._capture_pair(label + "-before")
        recording = self.record_transition(
            duration_ms, fps_target, label, pre_pair=pre,
            trigger=lambda: self.click_both(x, y, wait_processed=False))
        action = self.last_transition["triggeredAction"]
        state = self.kotonoha.status()
        if state.get("lastAction", {}).get("actionId") == action["actionId"]:
            action["kotonohaProcessed"] = state["lastAction"]
        comparison = self.compare_last_transition()
        analysis = self.analyze_transition("original")
        return {"action": action, "preFrame": pre, "recording": recording,
                "comparison": comparison,
                "originalVisualChange": analysis["firstVisualChange"],
                "originalPostFrame": analysis["postFrame"],
                "originalChoiceIntrospectionAvailable": False}

    def run_input_sequence(self, actions: list[dict]) -> dict:
        if len(actions) > 1000:
            raise ValueError("Sequence limit is 1000 actions")
        results = []
        for item in actions:
            action = item.get("action")
            if action == "wait":
                delay = int(item.get("ms", 0))
                if not 0 <= delay <= 60000:
                    raise ValueError("Wait must be 0..60000 ms")
                time.sleep(delay / 1000)
                results.append({"waited_ms": delay})
            elif action == "click":
                results.append(self.click_both(float(item["x"]), float(item["y"])))
            elif action == "key":
                results.append(self.key_both(str(item["key"])))
            elif action == "capture":
                results.append(self.capture_both(str(item.get("label", "sequence"))))
            elif action == "record_transition":
                results.append(self.record_transition(int(item.get("durationMs", 1000)),
                                                      int(item.get("fpsTarget", 30)),
                                                      str(item.get("label", "sequence"))))
            else:
                raise ValueError(f"Unsupported sequence action: {action}")
        return {"actions": len(results), "results": results}

    def get_parity_report(self) -> dict:
        OUTPUT.mkdir(parents=True, exist_ok=True)
        path = OUTPUT / "parity-report.json"
        path.write_text(json.dumps({"actions": self.actions, "results": self.report},
                                   ensure_ascii=False, indent=2), encoding="utf-8")
        return {"path": str(path), "actions": len(self.actions),
                "results": len(self.report),
                "lastTransition": self.last_transition["directory"] if self.last_transition else None}
