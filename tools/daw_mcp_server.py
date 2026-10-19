#!/usr/bin/env python3
"""MCP stdio server that bridges Claude Code to a running DAW's control socket.

The DAW opens a Unix socket speaking line-delimited JSON when control is enabled
in its settings.json (`"controlEnabled": true`). That is the only switch — the
DAW does not read DAW_CONTROL_ENABLE or any other env var. This server exposes
that channel as MCP tools, so an agent can drive the DAW and read its state.

The DAW loads settings.json relative to its working directory; running `./DAW`
from `build/` means editing `build/assets/settings.json`.

Dependency-free: implements the small slice of MCP needed (initialize, tools/list,
tools/call) by hand so it runs under a bare python3.

Run manually to test:  echo '{"jsonrpc":"2.0","id":1,"method":"tools/list"}' | python3 daw_mcp_server.py
"""
import json
import os
import socket
import sys

SERVER_NAME = "daw"
SERVER_VERSION = "1.0.0"
PROTOCOL_VERSION = "2024-11-05"
DEFAULT_SOCKET = "/tmp/microtonal-daw.sock"
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SETTINGS_PATH = os.path.join(REPO_ROOT, "build", "assets", "settings.json")
CALL_TIMEOUT = 30.0

# ---------------------------------------------------------------- socket client


def socket_path():
    env = os.environ.get("DAW_CONTROL_SOCKET")
    if env:
        return env
    for path in (SETTINGS_PATH, os.path.join(REPO_ROOT, "assets", "settings.json")):
        try:
            with open(path) as f:
                found = json.load(f).get("controlSocketPath")
            if found:
                return found
        except (OSError, ValueError):
            pass
    return DEFAULT_SOCKET


def daw_request(payload):
    """Sends one request to the DAW and returns the parsed reply.

    Connects per call: cheap on a Unix socket, and avoids holding a stale fd
    across a DAW restart.
    """
    path = socket_path()
    try:
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
            s.settimeout(CALL_TIMEOUT)
            s.connect(path)
            s.sendall(json.dumps(payload).encode() + b"\n")
            buf = b""
            while b"\n" not in buf:
                chunk = s.recv(65536)
                if not chunk:
                    raise ConnectionError("DAW closed the connection")
                buf += chunk
            return json.loads(buf.split(b"\n", 1)[0].decode())
    except FileNotFoundError:
        raise RuntimeError(
            f"no DAW control socket at {path} — the DAW is not running or control is off. "
            f"Set \"controlEnabled\": true in the settings.json the DAW reads "
            f"(assets/settings.json relative to its working directory, e.g. "
            f"build/assets/settings.json) and start it. There is no env var to enable it.")
    except ConnectionRefusedError:
        raise RuntimeError(f"DAW control socket at {path} refused the connection (DAW shutting down?)")
    except socket.timeout:
        raise RuntimeError(f"timed out after {CALL_TIMEOUT:.0f}s waiting on the DAW (blocked in a modal dialog?)")


def daw_op(op, **fields):
    """Sends one op and raises on a DAW-reported error, so tool calls surface it."""
    reply = daw_request({"op": op, **fields})
    if not reply.get("ok"):
        err = reply.get("error") or {}
        raise RuntimeError(f"{err.get('code', 'error')}: {err.get('message', 'unknown error')}")
    return reply.get("result")

# ---------------------------------------------------------------------- tools

COMMON_ARGS = {"args": {"type": "object", "description": "op-specific arguments"}}

TOOLS = [
    {
        "name": "daw",
        "description": ("Escape hatch: send a raw control request to the DAW. "
                        "ops: action, query, undo, redo, goto, save, load, list_actions, "
                        "describe_action, transport, shutdown."),
        "inputSchema": {
            "type": "object",
            "properties": {
                "op": {"type": "string", "description": "the op to run"},
                "action": {"type": "string", "description": "for op=action: the action name"},
                "query": {"type": "string", "description": "for op=query: the query name"},
                "args": {"type": "object", "description": "op-specific arguments"},
                "version": {"type": "array", "items": {"type": "integer"}, "description": "for op=goto"},
                "path": {"type": "string", "description": "for op=save/load"},
                "index": {"type": "integer", "description": "for op=redo"},
            },
            "required": ["op"],
        },
    },
    {
        "name": "daw_help",
        "description": ("The data model plus an index of every action, query and op. "
                        "CALL THIS FIRST when starting work in the DAW: it explains managerPath, "
                        "node ids, the pitch/rhythm vector encoding, the tuning model and the "
                        "read-live rule. Then use daw_describe_action / daw_describe_query for "
                        "the exact parameters and a worked example of each one. "
                        "If this call fails with 'no DAW control socket', the DAW is not running "
                        "or is not controllable: it must be started with \"controlEnabled\": true "
                        "in the assets/settings.json under its working directory (e.g. "
                        "build/assets/settings.json when run from build/). DAW_CONTROL_ENABLE is "
                        "not read by the DAW; DAW_CONTROL_SOCKET only changes which socket this "
                        "MCP server connects to."),
        "inputSchema": {"type": "object", "properties": {}},
    },
    {
        "name": "daw_describe_query",
        "description": "Full definition of one query: summary, arguments (with meanings) and an example.",
        "inputSchema": {
            "type": "object",
            "properties": {"query": {"type": "string"}},
            "required": ["query"],
        },
    },
    {
        "name": "daw_action",
        "description": ("Run a registered DAW action (undoable, replayed to the audio graph). "
                        "Actions take INTENT, not state — pass what you want; the DAW reads current "
                        "values itself. Get names from daw_list_actions and the exact arguments from "
                        "daw_describe_action."),
        "inputSchema": {
            "type": "object",
            "properties": {"name": {"type": "string"}, **COMMON_ARGS},
            "required": ["name", "args"],
        },
    },
    {
        "name": "daw_query",
        "description": ("Read DAW state as JSON. Common queries: nodes, node (ports+params), "
                        "connections, managers, graph, elements, region (notes), tracks, "
                        "vst_params, transport, audio, undo, settings, embedded_windows, "
                        "piano_rolls. Call daw_list_queries, or daw_describe_query for one in full."),
        "inputSchema": {
            "type": "object",
            "properties": {"query": {"type": "string"}, **COMMON_ARGS},
            "required": ["query"],
        },
    },
    {
        "name": "daw_list_actions",
        "description": "Every action name with a one-line summary.",
        "inputSchema": {"type": "object", "properties": {}},
    },
    {
        "name": "daw_list_queries",
        "description": "Every query name with a one-line summary.",
        "inputSchema": {"type": "object", "properties": {}},
    },
    {
        "name": "daw_describe_action",
        "description": "Full definition of one action: summary, every parameter with its meaning, and a worked example.",
        "inputSchema": {
            "type": "object",
            "properties": {"action": {"type": "string"}},
            "required": ["action"],
        },
    },
    {
        "name": "daw_capture",
        "description": ("Screenshot a DAW renderer and return it as a PNG image. target is \"main\" "
                        "(the node canvas, default) or a window title / id from daw_windows — e.g. "
                        "\"Piano Roll\", \"Arranger##2\". The reply is rendered fresh on the next frame. "
                        "Open windows with daw_window first."),
        "inputSchema": {"type": "object", "properties": {"target": {"type": "string"}}},
    },
    {
        "name": "daw_windows",
        "description": "List the main canvas plus every expanded window (title, id, sdlWindowID, size, visible).",
        "inputSchema": {"type": "object", "properties": {}},
    },
    {
        "name": "daw_window",
        "description": ("Open or close an expanded window, so you can capture and inspect it. "
                        "open: preferences, undo_tree, arranger (nodeID), patcher (nodeID), "
                        "sample_editor (nodeID, sampleIndex), piano_roll (nodeID, regionID). "
                        "close: a window title or numeric id."),
        "inputSchema": {
            "type": "object",
            "properties": {
                "open": {"type": "string"},
                "nodeID": {"type": "integer"},
                "regionID": {"type": "integer"},
                "sampleIndex": {"type": "integer"},
                "close": {"type": "string"},
            },
        },
    },
    {"name": "daw_undo", "description": "Undo one step.", "inputSchema": {"type": "object", "properties": {}}},
    {
        "name": "daw_redo",
        "description": "Redo one step (or a specific branch with index).",
        "inputSchema": {"type": "object", "properties": {"index": {"type": "integer"}}},
    },
    {
        "name": "daw_goto",
        "description": "Jump to a position in the undo tree, given as a child-index path from the root.",
        "inputSchema": {
            "type": "object",
            "properties": {"version": {"type": "array", "items": {"type": "integer"}}},
            "required": ["version"],
        },
    },
    {
        "name": "daw_transport",
        "description": "Transport control: play, stop, and/or seek to an absolute beat.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "play": {"type": "boolean"},
                "stop": {"type": "boolean"},
                "seekBeat": {"type": "number"},
            },
        },
    },
    {
        "name": "daw_save",
        "description": "Save the project (to `path` if given, else the project's current path).",
        "inputSchema": {"type": "object", "properties": {"path": {"type": "string"}}},
    },
    {
        "name": "daw_load",
        "description": "Load a project folder (rebuilds both graph copies).",
        "inputSchema": {"type": "object", "properties": {"path": {"type": "string"}}, "required": ["path"]},
    },
]

TOOL_NAMES = {t["name"] for t in TOOLS}


def call_tool(name, args):
    args = args or {}
    if name == "daw":
        payload = {"op": args.get("op")}
        if "action" in args:
            payload["action"] = args["action"]
        if "query" in args:
            payload["query"] = args["query"]
        if "args" in args:
            payload["args"] = args["args"]
        for k in ("version", "path", "index", "play", "stop", "seekBeat"):
            if k in args:
                payload[k] = args[k]
        reply = daw_request(payload)
        if not reply.get("ok"):
            err = reply.get("error") or {}
            raise RuntimeError(f"{err.get('code', 'error')}: {err.get('message', 'unknown error')}")
        return reply.get("result")

    if name == "daw_help":
        return daw_op("help")
    if name == "daw_capture":
        return daw_op("capture", **({"target": args["target"]} if "target" in args else {}))
    if name == "daw_windows":
        return daw_op("query", query="windows")
    if name == "daw_window":
        if "close" in args:
            return daw_op("window", close=args["close"])
        payload = {k: args[k] for k in ("open", "nodeID", "regionID", "sampleIndex") if k in args}
        return daw_op("window", **payload)
    if name == "daw_action":
        return daw_op("action", action=args["name"], args=args.get("args", {}))
    if name == "daw_query":
        return daw_op("query", query=args["query"], args=args.get("args", {}))
    if name == "daw_list_actions":
        return daw_op("list_actions")
    if name == "daw_list_queries":
        return daw_op("list_queries")
    if name == "daw_describe_action":
        return daw_op("describe_action", action=args["action"])
    if name == "daw_describe_query":
        return daw_op("describe_query", query=args["query"])
    if name == "daw_undo":
        return daw_op("undo")
    if name == "daw_redo":
        return daw_op("redo", **({"index": args["index"]} if "index" in args else {}))
    if name == "daw_goto":
        return daw_op("goto", version=args["version"])
    if name == "daw_transport":
        return daw_op("transport", **{k: args[k] for k in ("play", "stop", "seekBeat") if k in args})
    if name == "daw_save":
        return daw_op("save", **({"path": args["path"]} if "path" in args else {}))
    if name == "daw_load":
        return daw_op("load", path=args["path"])
    raise RuntimeError(f"unknown tool: {name}")

# ------------------------------------------------------------------ stdin loop


def send(msg):
    sys.stdout.write(json.dumps(msg) + "\n")
    sys.stdout.flush()


def handle(msg):
    method = msg.get("method")
    msg_id = msg.get("id")

    if method == "initialize":
        return {
            "protocolVersion": PROTOCOL_VERSION,
            "capabilities": {"tools": {}},
            "serverInfo": {"name": SERVER_NAME, "version": SERVER_VERSION},
        }
    if method in ("notifications/initialized", "initialized"):
        return None
    if method == "tools/list":
        return {"tools": TOOLS}
    if method == "tools/call":
        params = msg.get("params") or {}
        name = params.get("name")
        try:
            result = call_tool(name, params.get("arguments"))
            # A capture is an image: hand it to the agent as image content, with the
            # metadata (minus the base64 payload) as a text block beside it.
            if name == "daw_capture" and isinstance(result, dict) and result.get("data"):
                meta = {k: v for k, v in result.items() if k != "data"}
                return {"content": [
                    {"type": "text", "text": json.dumps(meta, indent=2)},
                    {"type": "image", "data": result["data"], "mimeType": "image/png"},
                ]}
            text = json.dumps(result, indent=2)
            return {"content": [{"type": "text", "text": text}]}
        except Exception as e:  # surfaced to the agent as a tool error
            return {"content": [{"type": "text", "text": str(e)}], "isError": True}
    if method == "ping":
        return {}
    if msg_id is None:
        return None
    raise ValueError(f"unknown method: {method}")


def main():
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            msg = json.loads(line)
        except ValueError:
            continue
        msg_id = msg.get("id")
        try:
            result = handle(msg)
        except Exception as e:
            if msg_id is not None:
                send({"jsonrpc": "2.0", "id": msg_id, "error": {"code": -32603, "message": str(e)}})
            continue
        if result is not None and msg_id is not None:
            send({"jsonrpc": "2.0", "id": msg_id, "result": result})


if __name__ == "__main__":
    main()
