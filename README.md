# microtonal-daw

currently only runs on linux until i get around to adding further support

Install SDL3/SDL_TTF -> for gui and text rendering
Install rtaudio -> for audio device functionality

get tinyfiledialogs source and header, link it in cmakelists

git clone https://github.com/brodybigwood/microtonal-daw.git

mkdir build 
cd build
cmake ..
make
./DAW

## Control channel (drive the DAW from scripts / Claude Code)

The DAW can expose a Unix socket that speaks line-delimited JSON, so an outside
process can perform any action the UI can and read any state as JSON. It is off
by default.

Enable it in `assets/settings.json` (or via env, which wins):

```json
{ "controlEnabled": true, "controlSocketPath": "/tmp/microtonal-daw.sock" }
```

```sh
DAW_CONTROL_ENABLE=1 DAW_CONTROL_SOCKET=/tmp/daw.sock ./DAW   # env override
```

Requests are one JSON object per line; responses echo the `id`:

```sh
tools/dawctl.py '{"op":"ping"}'
tools/dawctl.py '{"op":"query","query":"nodes"}'
tools/dawctl.py '{"op":"action","action":"add_node","args":{"managerPath":[],"nodeType":1,"x":300,"y":200}}'
tools/dawctl.py '{"op":"list_actions"}'
tools/dawctl.py '{"op":"describe_action","action":"create_note"}'
```

The channel is self-describing — call `help` for the data model (managerPath,
node ids, the pitch/rhythm vector encoding, the tuning model, the read-live
rule) plus an index of everything, then `describe_action` / `describe_query` for
one entry in full, with parameter meanings and a worked example:

```sh
tools/dawctl.py '{"op":"help"}'
tools/dawctl.py '{"op":"describe_action","action":"create_note"}'
tools/dawctl.py '{"op":"describe_query","query":"region"}'
```

Ops: `action`, `query`, `undo`, `redo`, `goto`, `save`, `load`, `transport`,
`help`, `list_actions`, `describe_action`, `list_queries`, `describe_query`,
`ping`, `shutdown`.
Queries: `graph`, `managers`, `nodes`, `node`, `params`, `connections`,
`node_types`, `transport`, `audio`, `undo`, `settings`, `arranger`, `tracks`,
`elements`, `region`, `embedded_windows`, `piano_rolls`.

Everything executes on the GUI thread (drained from `WindowHandler::tick`), which
is required — graph reads and mutations are only safe on the thread that owns the
graph copy.

### MCP

`tools/daw_mcp_server.py` bridges the socket to MCP, so an agent gets native
tools (`daw`, `daw_action`, `daw_query`, `daw_undo`, `daw_transport`, ...).
It is registered in `.mcp.json` (adjust `DAW_CONTROL_SOCKET` there if you use a
different socket path). The `daw` tool is a passthrough for anything the
dedicated tools don't cover.
