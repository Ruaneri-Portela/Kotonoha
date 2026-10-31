"""Exercise the actual SDL application's newline-delimited stdio MCP transport."""

import json
import os
from pathlib import Path
import queue
import subprocess
import sys
import tempfile
import threading
import time


class Client:
    def __init__(self, executable, root, *args):
        environment = os.environ.copy()
        for key in ("KOTONOHA_TIMELINE", "KOTONOHA_DEMO_VIDEO",
                    "KOTONOHA_DEMO_AUDIO", "KOTONOHA_TRANSLATION"):
            environment.pop(key, None)
        environment.update(SDL_VIDEO_DRIVER="dummy", SDL_AUDIO_DRIVER="dummy")
        self.log = tempfile.TemporaryFile()
        self.process = subprocess.Popen(
            [executable, "--mcp", "--mcp-root", str(root), *args],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=self.log,
            cwd=root, env=environment,
        )
        self.messages = queue.Queue()
        self.next_id = 0

        def read():
            for line in self.process.stdout:
                self.messages.put(line)
            self.messages.put(None)

        self.reader = threading.Thread(target=read, daemon=True)
        self.reader.start()

    def send(self, value):
        self.process.stdin.write((json.dumps(value) + "\n").encode())
        self.process.stdin.flush()

    def receive(self):
        line = self.messages.get(timeout=5)
        assert line is not None, "Engine closed stdout unexpectedly"
        result = json.loads(line)
        assert result["jsonrpc"] == "2.0", result
        return result

    def request(self, method, params=None):
        self.next_id += 1
        value = {"jsonrpc": "2.0", "id": self.next_id, "method": method}
        if params is not None:
            value["params"] = params
        self.send(value)
        response = self.receive()
        assert response["id"] == self.next_id, response
        return response

    def initialize(self, version="2025-11-25"):
        response = self.request("initialize", {
            "protocolVersion": version,
            "capabilities": {},
            "clientInfo": {"name": "kotonoha-test", "version": "1"},
        })
        assert response["result"]["serverInfo"]["name"] == "kotonoha-engine"
        self.send({"jsonrpc": "2.0", "method": "notifications/initialized"})
        return response["result"]

    def tool(self, name, arguments=None, error=False):
        response = self.request("tools/call", {
            "name": name, "arguments": arguments or {},
        })
        result = response["result"]
        assert result["isError"] == error, response
        if error:
            assert result["content"][0]["text"]
            return result
        return json.loads(result["content"][0]["text"])

    def finish(self, partial=None):
        if partial is not None:
            self.process.stdin.write(partial)
            self.process.stdin.flush()
        self.process.stdin.close()
        if partial:
            assert self.receive()["error"]["code"] == -32700
        code = self.process.wait(timeout=5)
        self.reader.join(timeout=1)
        assert code == 0, f"Engine exited with {code}"
        assert self.messages.get(timeout=1) is None, "Unexpected stdout output"

    def close(self):
        if self.process.poll() is None:
            self.process.kill()
            self.process.wait(timeout=5)
        self.reader.join(timeout=1)
        if self.process.stdin and not self.process.stdin.closed:
            self.process.stdin.close()
        self.process.stdout.close()
        self.log.seek(0)
        output = self.log.read().decode(errors="replace")
        self.log.close()
        return output


def exercise(executable, root):
    client = Client(executable, root)
    try:
        assert client.request("tools/list")["error"]["code"] == -32002
        client.process.stdin.write(b"not-json\n[]\n{} trailing\n")
        client.process.stdin.flush()
        assert client.receive()["error"]["code"] == -32700
        assert client.receive()["error"]["code"] == -32600
        assert client.receive()["error"]["code"] == -32700
        assert client.initialize()["protocolVersion"] == "2025-11-25"
        client.send({"jsonrpc": "2.0", "method": "notifications/cancelled",
                     "params": {"requestId": -1}})
        assert client.request("ping")["result"] == {}
        assert client.request("unknown")["error"]["code"] == -32601
        tools = client.request("tools/list")["result"]["tools"]
        names = {tool["name"] for tool in tools}
        assert {"engine_state", "list_nodes", "start_game", "jump_node",
                "choose", "save_game", "load_game", "dump_translation",
                "advance_time", "set_paused", "set_debug"} <= names
        assert all(tool["inputSchema"]["type"] == "object" for tool in tools)
        state = client.tool("engine_state")
        # The engine splash waits for its time or a click; agents start anyway.
        assert state["game_state"] in ("splash", "menu") and not state["textbox_visible"]
        assert not client.tool("list_nodes")["nodes"]
        client.tool("dump_translation", {"path": "empty.json"}, error=True)
        client.tool("set_paused", {"paused": True})
        state = client.tool("start_game")
        assert state["node_id"] == 100 and state["mode"] == "timed"
        nodes = client.tool("list_nodes")["nodes"]
        assert len(nodes) > 20 and all(node["name"] for node in nodes)
        state = client.tool("advance_time", {"seconds": 6})
        assert state["node_id"] == 0 and state["mode"] == "standard", state
        client.tool("advance", error=True)
        client.tool("set_paused", {"paused": False})
        assert client.tool("advance")["node_id"] == 0
        assert client.tool("advance")["node_id"] == 1
        state = client.tool("jump_node", {"node_id": 2})
        assert state["waiting_for_text"]
        player = "Alice \u96ea"
        state = client.tool("submit_text", {"text": player})
        assert state["node_id"] == 3 and player in state["dialogue"]
        assert not state["waiting_for_text"]
        state = client.tool("jump_node", {"node_id": 4})
        assert state["waiting_for_choice"] and len(state["choices"]) == 2
        client.tool("choose", {"index": 200}, error=True)
        assert client.tool("choose", {"index": 0})["node_id"] == 7
        client.tool("set_paused", {"paused": True})
        client.tool("jump_node", {"node_id": 30})
        state = client.tool("advance_time", {"seconds": 2})
        assert abs(state["seconds"] - 2) < 0.001
        client.tool("save_game", {"path": "test.vn"})
        client.tool("jump_node", {"node_id": 44})
        state = client.tool("load_game", {"path": "test.vn"})
        assert state["node_id"] == 30 and abs(state["seconds"] - 2) < 0.001
        saved = (root / "test.vn").read_text()
        assert player in saved
        client.tool("jump_node", {"node_id": 11})
        state = client.tool("advance_time", {"seconds": 5})
        assert state["node_id"] == 14, state  # Torch inventory survived load.
        client.tool("jump_node", {"node_id": 30})
        state = client.tool("advance_time", {"seconds": 60})
        assert state["node_id"] == 30 and state["waiting_for_choice"], state
        assert state["seconds"] == 7 and state["paused"]
        assert client.tool("choose", {"index": 0})["node_id"] == 31
        client.tool("jump_node", {"node_id": 40})
        state = client.tool("advance_time", {"seconds": 4})
        assert state["node_id"] == 44, state  # Default route, no late answer.
        client.tool("choose", {"index": 0}, error=True)
        state = client.tool("set_debug", {"visible": True, "fps": False,
                                          "node_clock": False})
        assert state["debug_visible"] and not state["show_fps"]
        assert not state["show_node_clock"]
        client.tool("jump_node", {"node_id": 30})
        client.tool("advance_time", {"seconds": 1}, error=True)
        client.tool("set_debug", {"visible": False})
        client.tool("dump_translation", {"path": "translation.json"})
        translation = json.loads((root / "translation.json").read_text())
        assert "30" in translation and "40" in translation
        assert "Esperar o sinal do companheiro" in translation["30"]
        for tool in ("save_game", "load_game", "dump_translation"):
            client.tool(tool, {"path": "../kotonoha-outside.vn"}, error=True)
        client.tool("load_game", {"path": "missing.vn"}, error=True)
        client.tool("jump_node", {"node_id": 987654}, error=True)
        assert client.tool("engine_state")["node_id"] == 30
        for name, args in (
            ("choose", {"index": -1}),
            ("choose", {"index": 0.5}),
            ("advance_time", {"seconds": 61}),
            ("set_paused", {"paused": "true"}),
            ("jump_node", {}),
            ("engine_state", {"unexpected": True}),
            ("submit_text", {"text": "\0"}),
        ):
            assert client.request("tools/call", {
                "name": name, "arguments": args,
            })["error"]["code"] == -32602
        assert client.request("tools/call", {"name": "unknown"})["error"]["code"] == -32602
        for value in ({"jsonrpc": "2.0", "id": None, "method": "ping"},
                      {"jsonrpc": "2.0\0invalid", "id": 0, "method": "ping"}):
            client.send(value)
            assert client.receive()["error"]["code"] == -32600
        outside = root.parent / "kotonoha-outside.vn"
        client.tool("save_game", {"path": str(outside)}, error=True)
        link = root / "escape"
        link.symlink_to(root.parent, target_is_directory=True)
        client.tool("save_game", {"path": "escape/kotonoha-outside.vn"},
                    error=True)
        assert not outside.exists()
        client.tool("jump_node", {"node_id": 40})
        client.tool("set_paused", {"paused": False})
        time.sleep(4.1)
        client.tool("choose", {"index": 0}, error=True)
        assert client.tool("engine_state")["node_id"] == 44
        # A partial line must never block SDL or prevent subsequent requests.
        client.process.stdin.write(b'{"jsonrpc":"2.0","id":"partial",')
        client.process.stdin.flush()
        time.sleep(0.03)
        client.process.stdin.write(b'"method":"ping"}\n')
        client.process.stdin.flush()
        assert client.receive()["id"] == "partial"
        # The transport must flush more than one frame's request budget.
        for index in range(40):
            client.send({"jsonrpc": "2.0", "id": f"batch-{index}", "method": "ping"})
        for index in range(40):
            assert client.receive()["id"] == f"batch-{index}"
        client.finish()
    finally:
        logs = client.close()
        if sys.exc_info()[0]:
            print(logs, file=sys.stderr)


def translation_and_eof(executable, root):
    (root / "custom.json").write_text(
        json.dumps({"0": {}, "nodo": {"Narrador": "Narrator"}}),
        encoding="utf-8")
    client = Client(executable, root, "--translation", str(root / "custom.json"))
    try:
        assert client.initialize("2024-11-05")["protocolVersion"] == "2024-11-05"
        client.tool("start_game")
        state = client.tool("jump_node", {"node_id": 0})
        assert state["speaker"] == "Narrator", state
        client.finish(b'{"incomplete":')
    finally:
        logs = client.close()
        if sys.exc_info()[0]:
            print(logs, file=sys.stderr)


if __name__ == "__main__":
    executable = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="kotonoha-mcp-") as directory:
        root = Path(directory) / "root"
        root.mkdir()
        exercise(executable, root)
        translation_and_eof(executable, root)
        environment = os.environ.copy()
        environment.update(SDL_VIDEO_DRIVER="dummy", SDL_AUDIO_DRIVER="dummy")
        oversize = subprocess.run(
            [executable, "--mcp", "--mcp-root", str(root)],
            input=b"x" * (1024 * 1024 + 1), capture_output=True,
            env=environment, timeout=5,
        )
        assert oversize.returncode != 0
        assert b"MCP input buffer exceeds 1 MiB" in oversize.stderr
        assert not oversize.stdout
    print("MCP protocol, story control, persistence and EOF tests passed")
