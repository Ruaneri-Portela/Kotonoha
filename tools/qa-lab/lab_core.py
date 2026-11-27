"""Kotonoha local QA lab: bounded, repeatable, synthetic Windows runs.

Only `tools/qa-lab/state/` is writable by lab code. Existing builds, route
metadata and synthetic fixture assets are read-only inputs. This is path
isolation, NOT an OS security sandbox for the launched native executable.
"""

from __future__ import annotations

from concurrent.futures import ThreadPoolExecutor, as_completed
from datetime import datetime, timezone
import hashlib
import itertools
import json
import os
from pathlib import Path
import random
import re
import shutil
import subprocess
import sys
import time
import uuid


HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
STATE = HERE / "state"
BUILD = REPO / "build" / "school-days-l4-two-dll-staticmd"
SOURCE_EXE = BUILD / "games" / "school-days" / "Release" / "SchoolDays.exe"
FIXTURE = BUILD / "games" / "school-days" / "tests" / "fixtures"
ROUTE = REPO / "games" / "school-days" / "game-assets" / "school-days-hq.ktnroute"
RUNTIME_FILES = ("SchoolDays.exe", "SDL3.dll", "SDL3_ttf.dll")
MAX_CASES = 256
TIMEOUT_MIN = 5
TIMEOUT_MAX = 120
RUN_ID = re.compile(r"^run-[0-9a-f]{12}$")


class LabError(RuntimeError):
    pass


def inside_state(path: Path) -> Path:
    """Refuse a path outside the dedicated state tree, including symlinks."""
    root = STATE.resolve()
    resolved = path.resolve()
    if resolved == root or root not in resolved.parents:
        raise LabError(f"Refusing path outside lab state: {path}")
    return resolved


def ensure_state() -> None:
    STATE.mkdir(parents=True, exist_ok=True)
    if STATE.is_symlink() or (hasattr(STATE, "is_junction") and STATE.is_junction()):
        raise LabError("The lab state directory must not be a symlink")
    for name in ("runs", "staged"):
        destination = inside_state(STATE / name)
        destination.mkdir(parents=True, exist_ok=True)


def run_folder(run_id: str) -> Path:
    if not RUN_ID.fullmatch(run_id):
        raise LabError("Invalid run ID")
    return inside_state(STATE / "runs" / run_id)


def read_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, data: dict) -> None:
    inside_state(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = inside_state(path.with_name(path.name + ".tmp"))
    temp.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")
    temp.replace(path)


def source_ok() -> dict:
    """Preflight never runs the retail EXE or inspects any private GPK."""
    missing = [str(SOURCE_EXE.parent / name) for name in RUNTIME_FILES
               if not (SOURCE_EXE.parent / name).is_file()]
    if not (FIXTURE / "Script").is_dir():
        missing.append(str(FIXTURE / "Script"))
    if not ROUTE.is_file():
        missing.append(str(ROUTE))
    if missing:
        raise LabError("Missing compiled L4 synthetic test inputs: " + ", ".join(missing))
    return {"exe": str(SOURCE_EXE), "fixture": str(FIXTURE),
            "route": str(ROUTE)}


def stage_runtime(run_id: str) -> Path:
    source_ok()
    digest = hashlib.sha256(SOURCE_EXE.read_bytes()).hexdigest()[:16]
    # One private staging directory per run avoids shared writes and handles
    # updates to executable dependencies safely.
    stage = inside_state(run_folder(run_id) / "staged-runtime")
    stage.mkdir(parents=True, exist_ok=True)
    # Stage/reuse the exact binary and DLL bytes inside our own directory.
    for filename in RUNTIME_FILES:
        source = SOURCE_EXE.parent / filename
        target = inside_state(stage / filename)
        source_hash = hashlib.sha256(source.read_bytes()).digest()
        if target.exists() and hashlib.sha256(target.read_bytes()).digest() == source_hash:
            continue
        temp = inside_state(stage / (filename + ".copying"))
        shutil.copy2(source, temp)
        if hashlib.sha256(temp.read_bytes()).digest() != source_hash:
            raise LabError("Staged runtime hash mismatch: " + filename)
        temp.replace(target)
    # The engine needs fonts to initialize even when test scenes are fully
    # synthetic. Shaders are required for optional GPU/OpenGL checks. All
    # resources are copied (not linked) to the lab's private staging area.
    assets = SOURCE_EXE.parent / "assets"
    if (not assets.is_dir() or assets.is_symlink()
            or (hasattr(assets, "is_junction") and assets.is_junction())):
        raise LabError("Built engine assets are missing or linked")
    for item in assets.rglob("*"):
        if item.is_symlink() or (hasattr(item, "is_junction") and item.is_junction()):
            raise LabError("Refusing linked engine asset: " + str(item))
        target = inside_state(stage / "assets" / item.relative_to(assets))
        if item.is_dir():
            target.mkdir(parents=True, exist_ok=True)
        elif item.is_file():
            target.parent.mkdir(parents=True, exist_ok=True)
            if (not target.is_file() or item.stat().st_size != target.stat().st_size
                    or hashlib.sha256(item.read_bytes()).digest() != hashlib.sha256(target.read_bytes()).digest()):
                shutil.copy2(item, target)
    return stage


def processes_busy() -> bool:
    if os.name != "nt":
        return False
    try:
        proc = subprocess.run(["tasklist", "/FI", "IMAGENAME eq SchoolDays.exe",
                               "/FO", "CSV", "/NH"], capture_output=True,
                              text=True, timeout=8, check=False)
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise LabError("Could not check existing SchoolDays.exe processes") from exc
    if proc.returncode != 0:
        raise LabError("Process check failed: " + proc.stderr[-300:])
    return '"SchoolDays.exe"' in proc.stdout


def available_modes() -> tuple[str, ...]:
    return ("smoke", "matrix", "gpu")


def generate_cases(mode: str, count: int, seed: int) -> list[dict]:
    if mode not in available_modes():
        raise LabError("mode must be smoke, matrix, or gpu")
    if not 1 <= count <= MAX_CASES:
        raise LabError(f"count must be 1..{MAX_CASES}")
    if not 0 <= seed <= 2**32 - 1:
        raise LabError("seed must be a uint32")
    drivers = ("software",) if mode != "gpu" else ("software", "direct3d12", "opengl")
    all_cases = list(itertools.product(drivers, (0, 1, 2, 4),
                                       (1.0, 1.5, 2.0, 4.0),
                                       (0, 1, 3, 14), (0.75, 1.0)))
    random.Random(seed).shuffle(all_cases)
    # Start with a deterministic baseline and ensure renderer/backend coverage
    # before sampling other permutations. These are deliberate edge cases;
    # the incompatible MPV combination should report its fallback, not fail.
    covered = [(driver, backend, 2.0, 0, 1.0)
               for driver in drivers for backend in (0, 1, 2, 4)]
    cases = (covered + [item for item in all_cases if item not in covered])[:count]
    return [dict(renderer=r, backend=b, factor=f, preset=p, renderScale=s)
            for r, b, f, p, s in cases]


def inspect_log(text: str, case: dict, exit_code: int | None) -> tuple[list[str], list[str]]:
    failures, evidence = [], []
    for line in text.splitlines():
        if any(word in line.lower() for word in
               ("engine started", "upscaler", "unavailable", "shader", "error")):
            evidence.append(line.strip()[:500])
    renderer = case["renderer"]
    backend = case["backend"]
    factor = case["factor"]
    if exit_code != 0:
        failures.append(f"process_exit={exit_code}")
    if f"Engine started ({renderer} renderer)" not in text:
        failures.append("renderer_start_not_confirmed")
    if factor > 1 and backend == 4 and renderer in ("opengl", "direct3d12"):
        expected = "Auto -> MPV" if renderer == "opengl" else "Auto -> Linear"
        if expected not in text:
            failures.append("auto_backend_resolution_not_confirmed")
    if factor > 1 and backend == 2 and renderer != "opengl":
        if "unavailable" not in text or "using Auto" not in text:
            failures.append("mpv_incompatible_fallback_not_confirmed")
    if factor > 1 and backend == 2 and renderer == "opengl":
        if "Upscaler backend=2" not in text:
            failures.append("mpv_backend_not_confirmed")
    return failures, evidence[-18:]


def compare_persisted_settings(case: dict, saved: dict) -> tuple[list[str], list[dict]]:
    """Classify known renderer normalization separately from unexpected drift."""
    expected = {"renderer": case["renderer"], "upscaler": case["backend"],
                "factor": case["factor"], "preset": case["preset"],
                "renderScale": case["renderScale"]}
    errors, normalized = [], []
    for key, requested in expected.items():
        actual = saved.get(key)
        if requested == actual:
            continue
        if (key == "upscaler" and requested == 2 and actual == 4
                and case["renderer"] != "opengl"):
            normalized.append({"setting": key, "requested": requested,
                               "saved": actual, "reason": "MPV requires OpenGL; engine selected Auto"})
            continue
        errors.append(f"settings_{key}_mismatch:requested={requested},saved={actual}")
    return errors, normalized


def one_case(run: Path, stage: Path, index: int, case: dict,
             timeout: int, gpu: bool) -> dict:
    case_dir = inside_state(run / f"case-{index:04d}")
    case_dir.mkdir(parents=True, exist_ok=False)
    docs = inside_state(case_dir / "documents")
    docs.mkdir()
    settings = inside_state(case_dir / "settings.json")
    engine_log = inside_state(case_dir / "engine.log")
    config = {"version": 1, "renderer": case["renderer"],
              "width": 960, "height": 540, "mode": 0,
              "resizable": False, "vsync": False,
              "upscaler": case["backend"], "preset": case["preset"],
              "factor": case["factor"], "renderScale": case["renderScale"],
              "shaderFiles": ""}
    write_json(settings, config)
    args = [str(stage / "SchoolDays.exe"), "--school-days", str(FIXTURE),
            "--ktrf", str(ROUTE), "--documents", str(docs),
            "--config", str(settings), "--no-launcher", "--no-asset-overlay",
            "--renderer", case["renderer"], "--node", "0", "--frames", "6",
            "--log", str(engine_log)]
    env = os.environ.copy()
    env.update({"SDL_VIDEODRIVER": "windows" if gpu and case["renderer"] != "software" else "dummy",
                "SDL_AUDIODRIVER": "dummy", "PATH": str(stage) + os.pathsep + env.get("PATH", "")})
    start = time.monotonic()
    try:
        kw = {"cwd": str(stage), "env": env, "capture_output": True,
              "text": True, "errors": "replace", "timeout": timeout, "check": False}
        if os.name == "nt":
            kw["creationflags"] = subprocess.CREATE_NO_WINDOW
        output = subprocess.run(args, **kw)
        exit_code = output.returncode
        stdout, stderr = output.stdout, output.stderr
        error = None
    except subprocess.TimeoutExpired as exc:
        exit_code, error = None, f"timeout after {timeout}s"
        stdout = (exc.stdout or b"").decode("utf-8", "replace") if isinstance(exc.stdout, bytes) else (exc.stdout or "")
        stderr = (exc.stderr or b"").decode("utf-8", "replace") if isinstance(exc.stderr, bytes) else (exc.stderr or "")
    except OSError as exc:
        exit_code, error = None, f"launch error: {exc}"
        stdout, stderr = "", str(exc)
    (case_dir / "stdout.txt").write_text(stdout, encoding="utf-8")
    (case_dir / "stderr.txt").write_text(stderr, encoding="utf-8")
    engine_text = engine_log.read_text(encoding="utf-8", errors="replace") if engine_log.is_file() else ""
    failures, evidence = inspect_log(engine_text, case, exit_code)
    saved_values, normalizations = {}, []
    try:
        persisted = read_json(settings)
        saved_values = {key: persisted.get(key) for key in
                        ("renderer", "upscaler", "factor", "preset", "renderScale")}
        settings_failures, normalizations = compare_persisted_settings(case, saved_values)
        failures.extend(settings_failures)
    except (OSError, ValueError, KeyError) as exc:
        failures.append("settings_not_readable:" + str(exc))
    if error:
        failures.append(error)
    result = {"id": index, "parameters": case, "pass": not failures,
              "seconds": round(time.monotonic() - start, 3),
              "exit": exit_code, "failures": failures, "evidence": evidence,
              "saved_settings": saved_values,
              "expected_normalizations": normalizations,
              "case_dir": str(case_dir)}
    write_json(case_dir / "result.json", result)
    return result


def start_job(mode: str = "smoke", count: int = 4,
              seed: int = 20261109, parallel: int = 2,
              timeout: int = 30) -> str:
    """Validate, create a job record, and start detached lab-only worker."""
    ensure_state()
    generate_cases(mode, count, seed)
    if not (1 <= parallel <= 4 and TIMEOUT_MIN <= timeout <= TIMEOUT_MAX):
        raise LabError("parallel 1..4; timeout 5..120 seconds")
    if mode == "gpu" and parallel != 1:
        raise LabError("Real GPU runs must use parallel=1")
    source_ok()
    if mode == "gpu" and processes_busy():
        raise LabError("A SchoolDays.exe is already running; GPU testing denied")
    run_id = "run-" + uuid.uuid4().hex[:12]
    run = run_folder(run_id)
    run.mkdir(parents=True, exist_ok=False)
    job = {"schema": 1, "run_id": run_id, "mode": mode, "count": count,
           "seed": seed, "parallel": parallel, "timeout": timeout,
           "created_utc": datetime.now(timezone.utc).isoformat(),
           "status": "queued"}
    write_json(run / "job.json", job)
    command = [sys.executable, str(HERE / "lab_cli.py"), "execute", "--run-id", run_id]
    try:
        logfile = (run / "worker-stdio.log").open("wb")
        kw = {"stdin": subprocess.DEVNULL, "stdout": logfile,
              "stderr": subprocess.STDOUT, "cwd": str(HERE),
              "close_fds": True}
        if os.name == "nt":
            kw["creationflags"] = subprocess.CREATE_NO_WINDOW | subprocess.DETACHED_PROCESS
        process = subprocess.Popen(command, **kw)
        logfile.close()
        # Child owns the job from this point forward. PID is informational;
        # never kill arbitrary PIDs based only on this saved value.
        job["pid"] = process.pid
        if read_json(run / "job.json")["status"] == "queued":
            write_json(run / "job.json", job)
    except Exception as exc:
        job["status"] = "launch_failed"
        job["error"] = str(exc)
        write_json(run / "job.json", job)
        raise LabError(f"Could not launch QA worker: {exc}") from exc
    return run_id


def execute_job(run_id: str) -> int:
    ensure_state()
    run = run_folder(run_id)
    job = read_json(run / "job.json")
    if job["status"] not in ("queued",):
        raise LabError("Job is not queued; refusing duplicate execution")
    job["status"] = "running"
    write_json(run / "job.json", job)
    results = []
    try:
        mode, count, seed = job["mode"], job["count"], job["seed"]
        gpu = mode == "gpu"
        if gpu and processes_busy():
            raise LabError("A SchoolDays.exe is already open; GPU test refused")
        stage = stage_runtime(run_id)
        cases = generate_cases(mode, count, seed)
        concurrency = 1 if gpu else job["parallel"]
        with ThreadPoolExecutor(max_workers=concurrency) as pool:
            futures = {pool.submit(one_case, run, stage, i + 1, item,
                                   job["timeout"], gpu): i + 1
                       for i, item in enumerate(cases)}
            for future in as_completed(futures):
                results.append(future.result())
                print(f"Case {len(results)}/{len(cases)}: "
                      f"{'PASS' if results[-1]['pass'] else 'FAIL'}", flush=True)
        results.sort(key=lambda item: item["id"])
        failures = [item for item in results if not item["pass"]]
        summary = {"schema": 1, "run_id": run_id, "mode": mode,
                   "count": len(results), "passed": len(results) - len(failures),
                   "failed": len(failures), "passed_all": not failures,
                   "seed": seed, "staged_binary": str(stage / "SchoolDays.exe"),
                   "cases": results,
                   "normalizations": [
                       {"case": item["id"], "changes": item["expected_normalizations"]}
                       for item in results if item["expected_normalizations"]],
                   "failures_only": [{"case": item["id"], "parameters": item["parameters"],
                                     "failures": item["failures"]} for item in failures]}
        write_json(run / "report.json", summary)
        job["status"] = "passed" if not failures else "failed"
        job["passed"], job["failed"] = summary["passed"], summary["failed"]
        job["finished_utc"] = datetime.now(timezone.utc).isoformat()
        write_json(run / "job.json", job)
        return 0 if not failures else 1
    except Exception as exc:
        job["status"] = "error"
        job["error"] = str(exc)
        job["finished_utc"] = datetime.now(timezone.utc).isoformat()
        write_json(run / "job.json", job)
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2


def status(run_id: str) -> dict:
    run = run_folder(run_id)
    job = read_json(run / "job.json")
    if (run / "report.json").is_file():
        report = read_json(run / "report.json")
        job["summary"] = {key: report[key] for key in
                          ("passed_all", "count", "passed", "failed", "failures_only")}
        job["summary"]["expected_normalizations"] = len(report.get("normalizations", []))
    return job


def list_runs(limit: int = 10) -> list[dict]:
    ensure_state()
    if not 1 <= limit <= 40:
        raise LabError("limit must be 1..40")
    runs = sorted((p for p in (STATE / "runs").iterdir()
                   if p.is_dir() and RUN_ID.fullmatch(p.name)),
                  key=lambda item: item.stat().st_mtime_ns, reverse=True)
    items = []
    for folder in runs[:limit]:
        try:
            job = status(folder.name)
            items.append({key: job[key] for key in
                          ("run_id", "status", "mode", "count", "created_utc")})
        except (LabError, ValueError, OSError):
            continue
    return items
