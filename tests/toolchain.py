"""Locate a host C compiler and build small C test programs.

The firmware itself is built with Keil. These tests only need a host compiler
to execute pure functions extracted from the real sources (CRC) and a stubbed
state-machine harness. If no compiler is present the affected tests skip.
"""

from __future__ import annotations

import os
import shutil
import subprocess
from dataclasses import dataclass, field
from pathlib import Path


@dataclass
class Toolchain:
    kind: str
    vcvars: Path | None = None
    cc: str | None = None
    extra: list[str] = field(default_factory=list)


def _find_vswhere() -> Path | None:
    candidates = [
        Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Microsoft Visual Studio/Installer/vswhere.exe",
        Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "Microsoft Visual Studio/Installer/vswhere.exe",
    ]
    for path in candidates:
        if path.is_file():
            return path
    return None


def find_toolchain() -> Toolchain | None:
    if os.name == "nt":
        vswhere = _find_vswhere()
        if vswhere:
            result = subprocess.run(
                [str(vswhere), "-latest", "-products", "*",
                 "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                 "-property", "installationPath"],
                capture_output=True, text=True, errors="replace", timeout=30,
            )
            root = result.stdout.strip().splitlines()
            if result.returncode == 0 and root:
                vcvars = Path(root[0]) / "VC/Auxiliary/Build/vcvars64.bat"
                if vcvars.is_file():
                    return Toolchain("msvc", vcvars=vcvars)
        if shutil.which("cl"):
            return Toolchain("msvc")
        return None
    cc = shutil.which("cc") or shutil.which("gcc")
    if cc:
        return Toolchain("gcc", cc=cc)
    return None


class Builder:
    def __init__(self, toolchain: Toolchain, workdir: Path):
        self.tc = toolchain
        self.workdir = Path(workdir)

    def _msvc_prefix(self) -> str:
        if self.tc.vcvars:
            return f'call "{self.tc.vcvars}" >nul && '
        return ""

    def _run(self, command: str) -> subprocess.CompletedProcess:
        result = subprocess.run(
            command, shell=True, cwd=self.workdir,
            capture_output=True, text=True, errors="replace", timeout=120,
        )
        if result.returncode != 0:
            raise AssertionError(
                "C build failed\ncommand: " + command +
                "\nstdout:\n" + (result.stdout or "") + "\nstderr:\n" + (result.stderr or "")
            )
        return result

    def compile(self, source: Path, obj: Path, includes=(), defines=(), charset: str | None = None,
                flags=()) -> Path:
        includes = [str(Path(p).resolve()) for p in includes]
        if self.tc.kind == "msvc":
            args = ["/nologo", "/W3", "/wd4819", "/c", "/TC", "/D_CRT_SECURE_NO_WARNINGS"]
            args += [f'/I"{p}"' for p in includes]
            args += [f"/D{d}" for d in defines]
            if charset:
                args.append(f"/source-charset:{charset}")
            args += list(flags)
            args += [f'/Fo"{obj}"', f'"{source}"']
            cmd = self._msvc_prefix() + "cl " + " ".join(args)
        else:
            args = ["-std=c99", "-O2", "-Wall", "-Wextra"]
            args += [f"-I{p}" for p in includes]
            args += [f"-D{d}" for d in defines]
            if charset == ".936":
                args.append("-finput-charset=GBK")
            args += list(flags)
            args += ["-c", str(source), "-o", str(obj)]
            cmd = " ".join([self.tc.cc] + args)
        self._run(cmd)
        return obj

    def link(self, objects, exe: Path) -> Path:
        if self.tc.kind == "msvc":
            files = " ".join(f'"{p}"' for p in objects)
            cmd = self._msvc_prefix() + f'cl /nologo /Fe:"{exe}" {files}'
        else:
            files = " ".join(str(p) for p in objects)
            cmd = f"{self.tc.cc} -o {exe} {files}"
        self._run(cmd)
        return exe


def run_exe(exe: Path, timeout: int = 30) -> subprocess.CompletedProcess:
    result = subprocess.run([str(exe)], capture_output=True, text=True, errors="replace", timeout=timeout)
    if result.returncode != 0:
        raise AssertionError(
            "test executable failed\nstdout:\n" + result.stdout + "\nstderr:\n" + result.stderr
        )
    return result
