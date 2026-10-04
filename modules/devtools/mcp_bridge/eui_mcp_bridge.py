#!/usr/bin/env python3
"""
EUI-NEO Model Context Protocol (MCP) Bridge
-------------------------------------------
Zero-dependency Stdio <-> HTTP Bridge for connecting AI assistants
(Cursor, Claude Desktop, VS Code Cline / Roo Code, etc.) to EUI-NEO applications.

Features:
- Dynamic/Random Port Allocation: Eliminates port conflicts.
- Auto App Lifecycle Management: Automatically launches the app if not running,
  and cleans up gracefully on exit.
- Active Instance Auto-Discovery: Connects to already running instances via discovery file.
- Standard JSON-RPC stdio protocol compliant.
"""

from __future__ import annotations

import argparse
import atexit
import json
import os
import pathlib
import signal
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

DISCOVERY_FILENAME = "eui_mcp_active.json"


def log(msg: str):
    """Log to stderr so stdout remains clean for MCP JSON-RPC protocol messages."""
    sys.stderr.write(f"[EUI-MCP-Bridge] {msg}\n")
    sys.stderr.flush()


def find_free_port() -> int:
    """Find a random available port on localhost."""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def get_discovery_path() -> pathlib.Path:
    return pathlib.Path(tempfile.gettempdir()) / DISCOVERY_FILENAME


def read_discovery_file() -> tuple[int | None, int | None]:
    """Returns (port, pid) if a valid discovery file exists."""
    path = get_discovery_path()
    if not path.is_file():
        return None, None
    try:
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
            port = data.get("port")
            pid = data.get("pid")
            return port, pid
    except Exception:
        return None, None


def is_port_responding(port: int, timeout: float = 0.5) -> bool:
    """Ping MCP server on given port."""
    url = f"http://127.0.0.1:{port}/mcp"
    req_body = json.dumps({"jsonrpc": "2.0", "id": "ping", "method": "ping"}).encode("utf-8")
    try:
        req = urllib.request.Request(
            url,
            data=req_body,
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return resp.status == 200
    except Exception:
        return False


def find_default_app_executable() -> pathlib.Path | None:
    """Search for clock executable in standard build directories."""
    repo_root = pathlib.Path(__file__).resolve().parents[3]
    candidates = [
        repo_root / "build" / "clock.exe",
        repo_root / "build" / "clock",
        repo_root / "build" / "apps" / "clock" / "clock.exe",
        repo_root / "build" / "apps" / "clock" / "clock",
    ]
    for c in candidates:
        if c.is_file():
            return c
    return None


class McpBridge:
    def __init__(self, port: int | None = None, app_path: str | None = None, auto_spawn: bool = True):
        self.port = port
        self.app_path = app_path
        self.auto_spawn = auto_spawn
        self.spawned_proc: subprocess.Popen | None = None

    def start(self):
        # 1. If explicit port was provided, test or use it
        if self.port is not None and self.port > 0:
            log(f"Using explicitly specified port: {self.port}")
            if not is_port_responding(self.port):
                if self.auto_spawn:
                    self._spawn_app(self.port)
                else:
                    log(f"Warning: No server responding on port {self.port} yet.")
            return

        # 2. Check discovery file for existing running instance
        disc_port, disc_pid = read_discovery_file()
        if disc_port and is_port_responding(disc_port):
            log(f"Discovered active running instance on port {disc_port} (PID: {disc_pid})")
            self.port = disc_port
            return

        # 3. Check fallback default port 8990
        if is_port_responding(8990):
            log("Detected active server on default port 8990")
            self.port = 8990
            return

        # 4. No active instance found: Allocate random free port and auto-spawn
        if self.auto_spawn:
            random_port = find_free_port()
            log(f"No running instance found. Allocating random port: {random_port}")
            self._spawn_app(random_port)
            self.port = random_port
        else:
            raise RuntimeError("No active EUI-NEO application found and auto-spawn is disabled.")

    def _spawn_app(self, port: int):
        exe = pathlib.Path(self.app_path) if self.app_path else find_default_app_executable()
        if not exe or not exe.is_file():
            raise FileNotFoundError(
                f"Application executable not found at '{exe}'. "
                "Please build the project first (e.g. 'ninja -C build clock') or specify --app."
            )

        log(f"Launching application: {exe} --mcp-server --mcp-port={port}")
        # Run in headless MCP mode (without opening devtools panel)
        args = [str(exe), "--mcp-server", f"--mcp-port={port}"]
        work_dir = exe.parent

        self.spawned_proc = subprocess.Popen(
            args,
            cwd=str(work_dir),
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )

        atexit.register(self.cleanup)

        # Wait for server to become responsive
        max_attempts = 30
        for i in range(max_attempts):
            if is_port_responding(port, timeout=0.3):
                log(f"Application ready and responding on port {port} (PID: {self.spawned_proc.pid})")
                return
            if self.spawned_proc.poll() is not None:
                raise RuntimeError(f"Application process exited unexpectedly with code {self.spawned_proc.returncode}")
            time.sleep(0.1)

        raise TimeoutError(f"Application started but did not respond on port {port} within 3 seconds.")

    def cleanup(self):
        if self.spawned_proc and self.spawned_proc.poll() is None:
            log(f"Cleaning up spawned process PID {self.spawned_proc.pid}...")
            try:
                self.spawned_proc.terminate()
                self.spawned_proc.wait(timeout=2.0)
            except Exception:
                try:
                    self.spawned_proc.kill()
                except Exception:
                    pass
            self.spawned_proc = None

    def run_proxy_loop(self):
        """Read standard MCP JSON-RPC messages from stdin and forward to HTTP."""
        log("Stdio <-> HTTP proxy loop running. Listening for MCP client requests...")
        url = f"http://127.0.0.1:{self.port}/mcp"

        while True:
            try:
                line = sys.stdin.readline()
                if not line:
                    log("Stdin reached EOF. Exiting bridge loop.")
                    break

                line = line.strip()
                if not line:
                    continue

                # Forward JSON-RPC request to HTTP
                req_data = line.encode("utf-8")
                req = urllib.request.Request(
                    url,
                    data=req_data,
                    headers={"Content-Type": "application/json"},
                    method="POST",
                )

                try:
                    with urllib.request.urlopen(req, timeout=10.0) as resp:
                        resp_data = resp.read().decode("utf-8")
                        sys.stdout.write(resp_data + "\n")
                        sys.stdout.flush()
                except urllib.error.HTTPError as e:
                    err_body = e.read().decode("utf-8")
                    sys.stdout.write(err_body + "\n")
                    sys.stdout.flush()
                except Exception as ex:
                    # Return standard JSON-RPC internal error if forward fails
                    err_resp = {
                        "jsonrpc": "2.0",
                        "id": None,
                        "error": {"code": -32603, "message": f"Bridge forward error: {str(ex)}"},
                    }
                    sys.stdout.write(json.dumps(err_resp) + "\n")
                    sys.stdout.flush()

            except KeyboardInterrupt:
                break
            except Exception as e:
                log(f"Error in proxy loop: {e}")
                break

        self.cleanup()


def main():
    parser = argparse.ArgumentParser(description="EUI-NEO MCP Stdio-to-HTTP Bridge")
    parser.add_argument(
        "--port",
        type=int,
        default=None,
        help="Target port (0 for random, or specify fixed port like 8990). Defaults to auto-detect/random.",
    )
    parser.add_argument(
        "--app",
        type=str,
        default=None,
        help="Path to the EUI-NEO application executable (defaults to ./build/clock.exe).",
    )
    parser.add_argument(
        "--no-spawn",
        action="store_true",
        help="Do not auto-spawn application if not running; only connect to existing instance.",
    )

    args = parser.parse_args()

    # Handle signal interrupts cleanly
    def sig_handler(sig, frame):
        sys.exit(0)

    signal.signal(signal.SIGINT, sig_handler)
    signal.signal(signal.SIGTERM, sig_handler)

    bridge = McpBridge(
        port=args.port,
        app_path=args.app,
        auto_spawn=not args.no_spawn,
    )
    try:
        bridge.start()
        bridge.run_proxy_loop()
    except Exception as e:
        log(f"Fatal error: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()
