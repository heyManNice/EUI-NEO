#!/usr/bin/env python3
"""
Backward compatibility proxy for eui_mcp_bridge.py -> eui_cli.py
"""
import sys
import runpy
from pathlib import Path

cli_script = Path(__file__).resolve().parent / "eui_cli.py"
sys.argv[0] = str(cli_script)
runpy.run_path(str(cli_script), run_name="__main__")
