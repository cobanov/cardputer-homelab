#!/usr/bin/env python3
"""fleet summary for the Cardputer homelab panel: GET /panel -> small JSON, GET / -> the same as a web page.

Runs `fleet --json` every 15 s in the background and serves only the fields the
panel shows. Raw fleet output includes process command lines (which can hold
tokens), so it is never passed through as is.
"""
import json, subprocess, threading, time, os
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

FLEET = os.path.expanduser("~/.local/bin/fleet")
PORT = int(os.environ.get("PORT", "8791"))
INTERVAL = 15
PAGE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "index.html")
cache = {"t": 0, "hosts": []}


def pct(used, total):
    return round(100 * used / total, 1) if used is not None and total else None


def slim(h):
    out = {"name": h.get("name") or h["host"], "ok": bool(h.get("ok"))}
    if not out["ok"]:
        return out
    disks = [m for m in h.get("mounts") or [] if not m["path"].startswith("/boot")]
    worst = max(disks, key=lambda m: pct(m["used_kib"], m["used_kib"] + m["avail_kib"]) or 0, default=None)
    gpu = (h.get("gpus") or [None])[0]
    cont = h.get("containers") or [None, None]
    out.update({
        "cpu": round(h["cpu_pct"], 1) if h.get("cpu_pct") is not None else None,
        "mem": pct(h.get("mem_used"), h.get("mem_total")),
        "mu": round((h.get("mem_used") or 0) / 1e9, 1),
        "mt": round((h.get("mem_total") or 0) / 1e9, 1),
        "disk": pct(worst["used_kib"], worst["used_kib"] + worst["avail_kib"]) if worst else None,
        "dpath": worst["path"] if worst else "",
        "temp": h.get("cpu_temp"),
        "cores": h.get("cores"),
        "load": (h.get("load") or [None])[0],
        "gpu": gpu.get("util") if gpu else None,
        "vram": pct(gpu.get("mem_used_mib"), gpu.get("mem_total_mib")) if gpu else None,
        "gname": (gpu.get("name") or "").replace("NVIDIA ", "").replace("GeForce ", "")[:16] if gpu else "",
        "crun": cont[0], "ctot": cont[1],
        "act": (h.get("activity") or "").replace("\u00b7", "-")[:30],  # the Cardputer font is ASCII only
    })
    return out


def refresh():
    while True:
        try:
            raw = subprocess.run([FLEET, "--json"], capture_output=True, text=True, timeout=60).stdout
            cache.update(t=int(time.time()), hosts=[slim(h) for h in json.loads(raw)])
        except Exception as e:  # retried next round, old data stays
            print("fleet failed:", e, flush=True)
        time.sleep(INTERVAL)


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/":  # the same data as a page for the browser
            body, kind = open(PAGE, "rb").read(), "text/html; charset=utf-8"
        elif self.path == "/panel":
            body, kind = json.dumps({"age": int(time.time()) - cache["t"], **cache}, separators=(",", ":")).encode(), "application/json"
        else:
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header("Content-Type", kind)
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *a):
        pass


if __name__ == "__main__":
    # self-check: slim() must never leak fields that can hold tokens
    s = slim({"host": "x", "ok": True, "cpu_pct": 1, "mem_used": 1, "mem_total": 2, "top": [{"cmd": "--token SECRET"}],
              "mounts": [{"path": "/", "used_kib": 1, "avail_kib": 3}], "activity": "idle"})
    assert "SECRET" not in json.dumps(s) and s["disk"] == 25.0 and s["mem"] == 50.0, s
    threading.Thread(target=refresh, daemon=True).start()
    ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
