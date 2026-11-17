#!/usr/bin/env python3
"""Tiny client for the Unreal editor's MCP server: mcp.py list | mcp.py call <tool> '<json args>'."""
import json, sys, urllib.request
URL = "http://127.0.0.1:8000/mcp"
HEAD = {"Content-Type": "application/json", "Accept": "application/json, text/event-stream"}
def post(body, session=None, timeout=300):
    head = dict(HEAD)
    if session: head["Mcp-Session-Id"] = session
    req = urllib.request.Request(URL, data=json.dumps(body).encode(), headers=head)
    with urllib.request.urlopen(req, timeout=timeout) as r:
        raw = r.read().decode("utf-8", "replace")
        sid = r.headers.get("Mcp-Session-Id")
    data = None
    if raw.strip().startswith("{"):
        data = json.loads(raw)
    else:
        for line in raw.splitlines():
            if line.startswith("data:"):
                data = json.loads(line[5:].strip())
    return data, sid
init, sid = post({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"protocolVersion": "2025-03-26", "capabilities": {}, "clientInfo": {"name": "horror-game-setup", "version": "1"}}})
try: post({"jsonrpc": "2.0", "method": "notifications/initialized"}, sid)
except Exception: pass
if sys.argv[1] == "list":
    tools = []; cursor = None
    while True:
        params = {"cursor": cursor} if cursor else {}
        res, _ = post({"jsonrpc": "2.0", "id": 2, "method": "tools/list", "params": params}, sid)
        tools += res["result"]["tools"]; cursor = res["result"].get("nextCursor")
        if not cursor: break
    json.dump(tools, open(sys.argv[2], "w"), indent=1)
    print(len(tools), "tools")
else:
    res, _ = post({"jsonrpc": "2.0", "id": 3, "method": "tools/call", "params": {"name": sys.argv[2], "arguments": json.loads(sys.argv[3]) if len(sys.argv) > 3 else {}}}, sid)
    print(json.dumps(res, indent=1)[:int(sys.argv[4]) if len(sys.argv) > 4 else 6000])
