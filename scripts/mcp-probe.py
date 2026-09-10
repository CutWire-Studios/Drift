#!/usr/bin/env python3
"""Measure Drift's MCP replies end to end.

Starts `drift --headless` over stdio, synthesises a four-shot test clip with ffmpeg, calls every
read tool and a sample of ops, and prints one line per call with the reply size in characters
(≈ tokens × 4), seconds, and the size of any image payload. Use it to keep an eye on the token
cost of the agent surface after changing the MCP layer.

    scripts/mcp-probe.py [--drift build/drift] [--out DIR] [--calls FILE]

--out saves every reply as JSON and every returned image next to it. --calls takes a file with
one call per line, {"tool": "...", "args": {...}} or {"method": "...", "params": {...}}; the
token VIDEO in an args value is replaced with the synthesised clip's path.
"""
import argparse
import base64
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time

DEFAULT_CALLS = [
    {"tool": "catalog", "args": {}},
    {"tool": "catalog", "args": {"brief": True}},
    {"tool": "search", "args": {"q": "fade", "schema": True}},
    {"tool": "toolbox", "args": {"ops": ["set_fade", "split_clip"]}},
    {"tool": "toolbox", "args": {"name": "timeline"}},
    {"tool": "import_media", "args": {"paths": ["VIDEO"]}},
    {"tool": "place_clip", "args": {"asset": 0, "at": 0}},
    {"tool": "add_text", "args": {"text": "Hello", "at": 1}},
    {"tool": "add_effect", "args": {"track": 1, "index": 0, "effect": "stylize.vignette"}},
    {"tool": "inspect", "args": {"clips": True, "detail": True}},
    {"tool": "list_effects", "args": {}},
    {"tool": "list_history", "args": {}},
    {"tool": "set_transform", "args": {"track": 1, "index": 0, "x": "abc"}},
    {"tool": "set_transformx", "args": {}},
    {"tool": "activity", "args": {"samples": 100}},
    {"tool": "frames", "args": {}},
    {"tool": "frames", "args": {"sample": "uniform", "n": 6}},
    {"tool": "get_waveform", "args": {"start": 0, "duration": 12, "image": True, "spectrogram": True}},
    {"tool": "detect_scenes", "args": {"track": 1, "index": 0}, "sleep": 6},
    {"tool": "list_scenes", "args": {"track": 1, "index": 0}},
    {"tool": "describe_clip", "args": {"track": 1, "index": 0}},
    {"tool": "frames", "args": {"sample": "scenes"}},
    {"tool": "capture", "args": {"at": 2.5}},
]


def write_test_clip(path):
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        sys.exit("ffmpeg is needed to synthesise the test clip")
    args = [ffmpeg, "-y", "-loglevel", "error",
            "-f", "lavfi", "-i", "testsrc2=size=640x360:rate=30:duration=3",
            "-f", "lavfi", "-i", "smptebars=size=640x360:rate=30:duration=3",
            "-t", "3", "-f", "lavfi", "-i", "mandelbrot=size=640x360:rate=30",
            "-f", "lavfi", "-i", "color=c=blue:size=640x360:rate=30:duration=3",
            "-f", "lavfi", "-i", "sine=frequency=440:beep_factor=4:sample_rate=48000:duration=12",
            "-filter_complex", "[0:v][1:v][2:v][3:v]concat=n=4:v=1:a=0[v]",
            "-map", "[v]", "-map", "4:a", "-c:v", "libx264", "-preset", "veryfast",
            "-pix_fmt", "yuv420p", "-c:a", "aac", "-shortest", path]
    subprocess.run(args, check=True)


class Server:
    def __init__(self, drift, stderr):
        env = dict(os.environ)
        env.setdefault("QT_QPA_PLATFORM", "xcb")
        self.proc = subprocess.Popen([drift, "--headless"], stdin=subprocess.PIPE,
                                     stdout=subprocess.PIPE, stderr=stderr, env=env)
        self.next_id = 0

    def rpc(self, method, params=None):
        self.next_id += 1
        msg = {"jsonrpc": "2.0", "id": self.next_id, "method": method}
        if params is not None:
            msg["params"] = params
        self.proc.stdin.write((json.dumps(msg) + "\n").encode())
        self.proc.stdin.flush()
        started = time.time()
        while True:
            line = self.proc.stdout.readline()
            if not line:
                sys.exit("server closed stdout")
            try:
                obj = json.loads(line)
            except json.JSONDecodeError:
                continue
            if obj.get("id") == self.next_id:
                return obj, time.time() - started

    def notify(self, method):
        self.proc.stdin.write((json.dumps({"jsonrpc": "2.0", "method": method}) + "\n").encode())
        self.proc.stdin.flush()

    def close(self):
        self.proc.stdin.close()
        try:
            self.proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            self.proc.kill()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--drift", default=os.path.join(os.path.dirname(__file__), "..", "build", "drift"))
    parser.add_argument("--out", help="directory to save replies and images into")
    parser.add_argument("--calls", help="file with one call per line (JSON)")
    opts = parser.parse_args()

    if opts.out:
        os.makedirs(opts.out, exist_ok=True)
    workdir = tempfile.mkdtemp(prefix="drift-mcp-probe-")
    video = os.path.join(workdir, "shots.mp4")
    write_test_clip(video)

    calls = DEFAULT_CALLS
    if opts.calls:
        calls = [json.loads(line) for line in open(opts.calls) if line.strip() and not line.startswith("#")]

    stderr = open(os.path.join(opts.out or workdir, "stderr.log"), "wb")
    server = Server(opts.drift, stderr)
    rows = []
    server.rpc("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                              "clientInfo": {"name": "mcp-probe", "version": "0"}})
    server.notify("notifications/initialized")
    listed, _ = server.rpc("tools/list")
    rows.append(("tools/list", len(json.dumps(listed)), 0, 0.0))

    for n, call in enumerate(calls, start=1):
        call = json.loads(json.dumps(call).replace("VIDEO", video))
        if "method" in call:
            reply, seconds = server.rpc(call["method"], call.get("params"))
            name = call["method"].replace("/", "_")
        else:
            reply, seconds = server.rpc("tools/call", {"name": call["tool"], "arguments": call.get("args", {})})
            name = call["tool"]
        image_bytes = 0
        for block in reply.get("result", {}).get("content", []):
            if block.get("type") == "image":
                image_bytes += len(block["data"])
                if opts.out:
                    ext = "png" if "png" in block.get("mimeType", "") else "jpg"
                    with open(os.path.join(opts.out, f"{n:02d}_{name}.{ext}"), "wb") as f:
                        f.write(base64.b64decode(block["data"]))
        if opts.out:
            with open(os.path.join(opts.out, f"{n:02d}_{name}.json"), "w") as f:
                json.dump(reply, f, indent=1)
        rows.append((name, len(json.dumps(reply)) - image_bytes, image_bytes, seconds))
        if call.get("sleep"):
            time.sleep(call["sleep"])

    server.close()
    shutil.rmtree(workdir, ignore_errors=True)
    print(f"{'call':24} {'chars':>8} {'image b64':>10} {'secs':>6}")
    for name, chars, image_bytes, seconds in rows:
        print(f"{name:24} {chars:8} {image_bytes:10} {seconds:6.2f}")


if __name__ == "__main__":
    main()
