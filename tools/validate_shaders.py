#!/usr/bin/env python3
"""
Validates every GLSL shader in ExoSim with the Khronos reference compiler
(glslangValidator): the shaders embedded as raw string literals in
src/Renderer.cpp, plus the files in shaders/. Exits non-zero on any
failure. Shader errors otherwise only show up at runtime on a real GPU -
this is how a GLSL built-in name collision (`noise3`) was caught.

Usage:  python tools/validate_shaders.py   (needs glslangValidator on PATH)
"""
import pathlib, re, shutil, subprocess, sys, tempfile

root = pathlib.Path(__file__).resolve().parent.parent
if not shutil.which("glslangValidator"):
    sys.exit("glslangValidator not found (Ubuntu: apt install glslang-tools)")

jobs = []
src = (root / "src" / "Renderer.cpp").read_text(encoding="utf-8")
with tempfile.TemporaryDirectory() as tmp:
    for name, body in re.findall(r'const char\* (k\w+) = R"\((.*?)\)";', src, re.S):
        ext = ".vert" if name.endswith("VS") else ".frag"
        path = pathlib.Path(tmp) / f"{name}{ext}"
        path.write_text(body.lstrip(), encoding="utf-8")
        jobs.append((f"Renderer.cpp:{name}", path))
    for f in sorted((root / "shaders").glob("*")):
        if f.suffix in (".vert", ".frag"):
            jobs.append((f"shaders/{f.name}", f))

    failures = 0
    for label, path in jobs:
        r = subprocess.run(["glslangValidator", str(path)], capture_output=True, text=True)
        ok = r.returncode == 0
        failures += not ok
        print(f"[{'OK' if ok else 'FAIL'}] {label}")
        if not ok:
            print(r.stdout.strip())

print(f"{len(jobs)} shaders, {failures} failed")
sys.exit(1 if failures or not jobs else 0)
