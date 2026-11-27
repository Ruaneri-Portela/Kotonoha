"""Fast offline tests; safe to run without building or launching the game."""

import json
from pathlib import Path
import subprocess
import sys
import unittest

import lab_core as lab
import lab_cli
import lab_mcp


class LabSafetyTests(unittest.TestCase):
    def test_human_readable_terminal_progress(self):
        case = {"id": 3, "parameters": {"renderer": "software", "backend": 2,
                "factor": 2.0, "preset": 0, "renderScale": 1.0}, "pass": True,
                "expected_normalizations": [{"reason": "MPV -> Auto"}], "failures": []}
        line = lab_cli.format_case_progress(case, 3, 32)
        self.assertIn("[  3/32]", line)
        self.assertIn("software backend=2", line)
        self.assertIn("PASS", line)
        self.assertIn("normalized=1", line)
        case["pass"] = False
        case["failures"] = ["renderer_start_not_confirmed"]
        self.assertIn("renderer_start_not_confirmed", lab_cli.format_case_progress(case, 3, 32))
    def test_rejects_path_traversal_and_unknown_run(self):
        for bad in ("../run-123456789abc", "run-../../", "run-1234", "run-123456789abc/.."): 
            with self.subTest(bad=bad), self.assertRaises(lab.LabError):
                lab.run_folder(bad)

    def test_case_matrix_reproducibility_and_bounds(self):
        one = lab.generate_cases("matrix", 40, 999)
        self.assertEqual(one, lab.generate_cases("matrix", 40, 999))
        self.assertNotEqual(one, lab.generate_cases("matrix", 40, 1000))
        self.assertEqual(len(one), 40)
        self.assertEqual(set(item["renderer"] for item in one), {"software"})
        gpu = lab.generate_cases("gpu", 12, 999)
        self.assertEqual({(x["renderer"], x["backend"]) for x in gpu},
                         {(r, b) for r in ("software", "opengl", "direct3d12")
                          for b in (0, 1, 2, 4)})
        for mode, count in (("invalid", 2), ("smoke", 0), ("gpu", 257)):
            with self.subTest(mode=mode, count=count), self.assertRaises(lab.LabError):
                lab.generate_cases(mode, count, 42)

    def test_log_policy_detects_backend_and_renderer(self):
        base = {"renderer": "opengl", "backend": 4, "factor": 2.0}
        failures, _ = lab.inspect_log("Engine started (opengl renderer)\nAuto -> MPV\n", base, 0)
        self.assertEqual(failures, [])
        failures, _ = lab.inspect_log("Engine started (opengl renderer)\n", base, 0)
        self.assertIn("auto_backend_resolution_not_confirmed", failures)
        failures, _ = lab.inspect_log("Engine started (direct3d12 renderer)\nunavailable; using Auto\n",
                                       {"renderer": "direct3d12", "backend": 2, "factor": 2.0}, 0)
        self.assertEqual(failures, [])

    def test_expected_config_normalization_is_not_unexpected_regression(self):
        case = {"renderer": "software", "backend": 2, "factor": 2.0,
                "preset": 0, "renderScale": 1.0}
        saved = {"renderer": "software", "upscaler": 4, "factor": 2.0,
                 "preset": 0, "renderScale": 1.0}
        failures, changes = lab.compare_persisted_settings(case, saved)
        self.assertEqual(failures, [])
        self.assertEqual(len(changes), 1)
        saved["renderScale"] = 0.75
        failures, _ = lab.compare_persisted_settings(case, saved)
        self.assertTrue(any("renderScale" in issue for issue in failures))

    def test_mcp_rejects_unexpected_args_and_unknown_tool(self):
        with self.assertRaises(lab.LabError):
            lab_mcp.call("lab_runs", {"path": "D:/Dev"})
        with self.assertRaises(lab.LabError):
            lab_mcp.call("run_shell", {})
        response = lab_mcp.handle({"jsonrpc": "2.0", "id": 2,
                                   "method": "tools/list", "params": {}})
        names = {tool["name"] for tool in response["result"]["tools"]}
        self.assertEqual(names,
                         {"lab_preflight", "lab_start", "lab_status", "lab_runs", "lab_failures"})
        fail = lab_mcp.handle({"jsonrpc": "2.0", "id": 3, "method": "tools/call",
                               "params": {"name": "lab_status", "arguments": {"run_id": "../etc"}}})
        self.assertTrue(fail["result"]["isError"])

    def test_mcp_stdio_handshake_over_actual_process(self):
        server = Path(__file__).parent / "lab_mcp.py"
        proc = subprocess.Popen([sys.executable, str(server)],
                                stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True, encoding="utf-8")
        messages = [
            {"jsonrpc": "2.0", "id": 1, "method": "initialize",
             "params": {"protocolVersion": "2025-11-25", "capabilities": {},
                        "clientInfo": {"name": "lab-unit-test", "version": "1"}}},
            {"jsonrpc": "2.0", "method": "notifications/initialized"},
            {"jsonrpc": "2.0", "id": 2, "method": "tools/list"},
            {"jsonrpc": "2.0", "id": 3, "method": "tools/call",
             "params": {"name": "lab_preflight", "arguments": {}}},
        ]
        payload = "\n".join(json.dumps(m) for m in messages) + "\n"
        stdout, stderr = proc.communicate(payload, timeout=15)
        self.assertEqual(proc.returncode, 0, stderr)
        replies = [json.loads(line) for line in stdout.splitlines()]
        self.assertEqual([msg["id"] for msg in replies], [1, 2, 3])
        self.assertEqual(replies[0]["result"]["serverInfo"]["name"], "kotonoha-qa-lab")
        self.assertFalse(replies[2]["result"]["isError"])
        self.assertIn("ready", replies[2]["result"]["content"][0]["text"])


if __name__ == "__main__":
    unittest.main()
