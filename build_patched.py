#!/usr/bin/env python
"""Patch to disable component manager version checking."""
import os
import sys

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
if hasattr(sys.stderr, "reconfigure"):
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")

# Add ESP-IDF venv to path
sys.path.insert(0, r"C:\Espressif\tools\python\v6.0\venv\Lib\site-packages")

# Patch the dependencies module BEFORE it's used
import idf_component_manager.dependencies as dep_module


def patched_check(*args, **kwargs):
    print("Skipping component version check due to registry compatibility issue")
    return None


dep_module.check_for_new_component_versions = patched_check

# Now run the build
os.chdir(r"c:\Users\user\Documents\projects\SmartAttendance")
os.environ["IDF_SKIP_COMPONENT_MANAGER_VERSION_CHECK"] = "1"

# Import and run idf.py
sys.path.insert(0, r"C:\esp\v6.0\esp-idf\tools")
from idf import main

sys.exit(main(["reconfigure", "build"]))
