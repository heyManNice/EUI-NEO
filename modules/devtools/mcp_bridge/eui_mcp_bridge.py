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

# Ensure stdout and stdin use UTF-8 regardless of OS code page (cp936/GBK on Windows)
if hasattr(sys.stdout, "reconfigure"):
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
        sys.stdin.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

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


def stop_running_app() -> tuple[bool, int | None, int | None]:
    """Terminate any background running EUI-NEO application and clean discovery file."""
    disc_port, disc_pid = read_discovery_file()
    stopped = False
    if disc_pid:
        try:
            if sys.platform == "win32":
                subprocess.run(["taskkill", "/PID", str(disc_pid), "/F"], capture_output=True)
            else:
                os.kill(disc_pid, signal.SIGTERM)
            stopped = True
        except Exception as e:
            log(f"Warning: Failed to terminate PID {disc_pid}: {e}")

    disc_path = get_discovery_path()
    if disc_path.exists():
        try:
            disc_path.unlink()
        except Exception:
            pass

    return stopped, disc_pid, disc_port


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
    def __init__(self, port: int | None = None, app_path: str | None = None, auto_spawn: bool = True, daemon: bool = False):
        self.port = port
        self.app_path = app_path
        self.auto_spawn = auto_spawn
        self.daemon = daemon
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

        creationflags = 0
        if self.daemon and sys.platform == "win32":
            creationflags = subprocess.DETACHED_PROCESS | subprocess.CREATE_NEW_PROCESS_GROUP | 0x01000000

        try:
            self.spawned_proc = subprocess.Popen(
                args,
                cwd=str(work_dir),
                stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                creationflags=creationflags,
                close_fds=True,
            )
        except OSError:
            creationflags = 0
            if self.daemon and sys.platform == "win32":
                creationflags = subprocess.DETACHED_PROCESS | subprocess.CREATE_NEW_PROCESS_GROUP
            self.spawned_proc = subprocess.Popen(
                args,
                cwd=str(work_dir),
                stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                creationflags=creationflags,
                close_fds=True,
            )

        if not self.daemon:
            atexit.register(self.cleanup)

        # Wait for server to become responsive
        max_attempts = 35
        for i in range(max_attempts):
            if is_port_responding(port, timeout=0.3):
                log(f"Application ready and responding on port {port} (PID: {self.spawned_proc.pid})")
                return
            if not self.daemon and self.spawned_proc.poll() is not None:
                raise RuntimeError(f"Application process exited unexpectedly with code {self.spawned_proc.returncode}")
            time.sleep(0.1)

        raise TimeoutError(f"Application started but did not respond on port {port} within 3.5 seconds.")

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
                    sys.stdout.write(json.dumps(err_resp, ensure_ascii=False) + "\n")
                    sys.stdout.flush()

            except KeyboardInterrupt:
                break
            except Exception as e:
                log(f"Error in proxy loop: {e}")
                break

    def execute_tool(self, tool_name: str, arguments: dict) -> dict:
        """Directly invoke a tool via HTTP and return the result dictionary."""
        url = f"http://127.0.0.1:{self.port}/mcp"
        payload = {
            "jsonrpc": "2.0",
            "id": "cli_call",
            "method": "tools/call",
            "params": {
                "name": tool_name,
                "arguments": arguments,
            },
        }
        req_data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        req = urllib.request.Request(
            url,
            data=req_data,
            headers={"Content-Type": "application/json; charset=utf-8"},
            method="POST",
        )
        try:
            with urllib.request.urlopen(req, timeout=15.0) as resp:
                resp_data = resp.read().decode("utf-8")
                return json.loads(resp_data)
        except Exception as e:
            return {"jsonrpc": "2.0", "id": "cli_call", "error": {"code": -32603, "message": str(e)}}


def handle_cli_call(args, extra_args):
    """Direct single-line CLI tool execution."""
    bridge = McpBridge(
        port=args.port,
        app_path=args.app,
        auto_spawn=not args.no_spawn,
        daemon=True,
    )
    bridge.start()

    # Parse extra key-value arguments: --key value or --flag
    tool_args = {}
    i = 0
    while i < len(extra_args):
        arg = extra_args[i]
        if arg.startswith("--"):
            key = arg[2:]
            if "=" in key:
                k, v = key.split("=", 1)
                tool_args[k] = v
                i += 1
                continue
            if i + 1 < len(extra_args) and not extra_args[i + 1].startswith("--"):
                val = extra_args[i + 1]
                # String parameters that should NEVER be converted to numbers
                if key in ("target", "elementId", "id", "key", "text", "mode", "filePath", "path"):
                    tool_args[key] = str(val)
                elif val.lower() == "true":
                    tool_args[key] = True
                elif val.lower() == "false":
                    tool_args[key] = False
                else:
                    try:
                        if "." in val:
                            tool_args[key] = float(val)
                        else:
                            tool_args[key] = int(val)
                    except ValueError:
                        tool_args[key] = val
                i += 2
            else:
                # Boolean flag
                tool_args[key] = True
                i += 1
        else:
            i += 1

    tool_name = args.tool
    res = bridge.execute_tool(tool_name, tool_args)

    if "error" in res:
        sys.stderr.write(f"Error ({res['error'].get('code')}): {res['error'].get('message')}\n")
        sys.exit(1)

    result_obj = res.get("result", {})
    contents = result_obj.get("content", [])

    for c in contents:
        ctype = c.get("type")
        if ctype == "text":
            text = c.get("text", "")
            # If the text is JSON, try pretty-printing it if compact is not requested
            try:
                parsed = json.loads(text)
                print(json.dumps(parsed, indent=2, ensure_ascii=False))
            except Exception:
                print(text)
        elif ctype == "image":
            data_b64 = c.get("data", "")
            out_file = getattr(args, "output", None)
            if out_file:
                import base64
                img_data = base64.b64decode(data_b64)
                with open(out_file, "wb") as f:
                    f.write(img_data)
                print(f"Screenshot saved to: {out_file} ({len(img_data)} bytes)")
            else:
                print(f"[Image captured: base64 len {len(data_b64)} chars. Use --output <file.png> to save directly]")


def main():
    parser = argparse.ArgumentParser(description="EUI-NEO MCP Stdio-to-HTTP Bridge & CLI")
    parser.add_argument(
        "--port",
        type=int,
        default=None,
        help="Target port (0 for random, or specify fixed port like 8990). Defaults to auto-detect/random.",
    )
    parser.add_argument(
        "--print-port",
        action="store_true",
        help="Print the active MCP server port and exit.",
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

    subparsers = parser.add_subparsers(dest="subcommand", help="Subcommand to run")

    # Top-level direct shortcuts:
    subparsers.add_parser("snapshot", help="Take accessibility text snapshot")
    
    click_p = subparsers.add_parser("click", help="Click an element (e.g. click e2 or click <id>)")
    click_p.add_argument("target", type=str, help="Target ref handle (e.g. e2) or element ID")
    
    fill_p = subparsers.add_parser("fill", help="Fill/replace text in an element (e.g. fill e5 'Beijing')")
    fill_p.add_argument("target", type=str, help="Target ref handle (e.g. e5) or element ID")
    fill_p.add_argument("text", type=str, help="Text to input")
    fill_p.add_argument("--append", action="store_true", help="Append text instead of replace")

    press_p = subparsers.add_parser("press", help="Press keyboard key (e.g. press Enter)")
    press_p.add_argument("key", type=str, help="Key name (Enter, Backspace, Escape, Tab, etc.)")
    press_p.add_argument("--target", type=str, default="", help="Target ref handle or element ID")

    shot_p = subparsers.add_parser("shot", help="Take viewport screenshot and save to disk")
    shot_p.add_argument("--out", type=str, default="eui_screenshot.png", help="Output PNG file path (default: eui_screenshot.png)")

    subparsers.add_parser("stop", help="Stop background running EUI-NEO application")

    # General 'call' subcommand for invoking any MCP tool
    call_parser = subparsers.add_parser("call", help="Directly invoke an MCP tool in a single line")
    call_parser.add_argument("tool", type=str, help="Tool name to call (e.g. describe_screen, click_element, input_text, capture_viewport)")
    call_parser.add_argument("--output", type=str, default=None, help="Output file path for screenshot images (e.g. out.png)")

    args, extra_args = parser.parse_known_args()

    # Handle signal interrupts cleanly
    def sig_handler(sig, frame):
        sys.exit(0)

    signal.signal(signal.SIGINT, sig_handler)
    signal.signal(signal.SIGTERM, sig_handler)

    if args.print_port:
        disc_port, disc_pid = read_discovery_file()
        if disc_port and is_port_responding(disc_port):
            print(f"Port: {disc_port} (PID: {disc_pid})")
            sys.exit(0)
        elif is_port_responding(8990):
            print("Port: 8990")
            sys.exit(0)
        else:
            print("No active EUI MCP server found.")
            sys.exit(1)

    if args.subcommand == "snapshot":
        args.tool = "take_snapshot"
        args.output = None
        handle_cli_call(args, extra_args)
        return

    if args.subcommand == "click":
        args.tool = "click_element"
        args.output = None
        extra_args = ["--target", args.target] + extra_args
        handle_cli_call(args, extra_args)
        return

    if args.subcommand == "fill":
        args.tool = "input_text"
        args.output = None
        mode = "append" if args.append else "replace"
        extra_args = ["--target", args.target, "--text", args.text, "--mode", mode] + extra_args
        handle_cli_call(args, extra_args)
        return

    if args.subcommand == "press":
        args.tool = "press_key"
        args.output = None
        extra_args = ["--key", args.key]
        if args.target:
            extra_args += ["--target", args.target]
        handle_cli_call(args, extra_args)
        return

    if args.subcommand == "shot":
        args.tool = "capture_viewport"
        args.output = args.out
        extra_args = ["--filePath", args.out]
        handle_cli_call(args, extra_args)
        return

    if args.subcommand == "stop":
        stopped, pid, port = stop_running_app()
        if stopped:
            print(f"Stopped EUI-NEO application (PID: {pid}, Port: {port})")
        else:
            print("No active EUI-NEO application found to stop.")
        sys.exit(0)

    if args.subcommand == "call":
        handle_cli_call(args, extra_args)
        return

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
