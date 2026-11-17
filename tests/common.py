"""Shared helpers for the host-side test suite (standard library only)."""

from __future__ import annotations

import re
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
FW = REPO / "AIC_MedRobot_stm32f407"
QT = REPO / "AICMedicineRobotSystem"


def read_text(path: Path, encoding: str = "utf-8") -> str:
    return Path(path).read_text(encoding=encoding)


def read_utf8(path: Path) -> str:
    return read_text(path, "utf-8-sig")


def read_gbk(path: Path) -> str:
    return read_text(path, "gbk")


def strip_c_comments(text: str) -> str:
    """Remove C comments while preserving string and character literals."""
    out = []
    i = 0
    n = len(text)
    state = "code"
    while i < n:
        ch = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if state == "code":
            if ch == "/" and nxt == "/":
                state = "line"
                out.append(" ")
                i += 2
                continue
            if ch == "/" and nxt == "*":
                state = "block"
                out.append(" ")
                i += 2
                continue
            out.append(ch)
            if ch == '"':
                state = "string"
            elif ch == "'":
                state = "char"
            i += 1
        elif state == "line":
            if ch == "\n":
                state = "code"
                out.append(ch)
            else:
                out.append(" ")
            i += 1
        elif state == "block":
            if ch == "*" and nxt == "/":
                state = "code"
                out.append(" ")
                i += 2
                continue
            out.append("\n" if ch == "\n" else " ")
            i += 1
        else:  # string or char literal
            out.append(ch)
            if ch == "\\" and i + 1 < n:
                out.append(text[i + 1])
                i += 2
                continue
            if (state == "string" and ch == '"') or (state == "char" and ch == "'"):
                state = "code"
            i += 1
    return "".join(out)


def c_function(text: str, name: str, include_signature: bool = True) -> str:
    """Return a C function definition (return type + signature + body) by name."""
    pattern = r"\b" + re.escape(name) + r"\s*\([^;{}]*\)\s*\{"
    match = re.search(pattern, text)
    if not match:
        raise AssertionError("C function not found: " + name)

    # Include the return type (for example ``uint16_t``) so extracted code is
    # valid C99. Function definitions in this project are written on one line.
    line_start = text.rfind("\n", 0, match.start()) + 1
    prefix = text[line_start:match.start()]
    if any(token in prefix for token in (";", "{", "}", "//", "/*", "#")):
        start = match.start()
    else:
        start = line_start
    brace = match.end() - 1
    depth = 0
    for i in range(brace, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[start:i + 1] if include_signature else text[brace:i + 1]
    raise AssertionError("Unbalanced braces in C function: " + name)


def define_int(text: str, name: str) -> int:
    """Parse an integer #define such as '#define X 500' or '#define X 30000UL'."""
    match = re.search(
        r"^\s*#\s*define\s+" + re.escape(name) + r"\s+([0-9]+)\s*(?:UL|U|L)?\b[^\n]*$",
        text,
        re.MULTILINE,
    )
    if not match:
        raise AssertionError("#define not found: " + name)
    return int(match.group(1))


def count_occurrences(text: str, needle: str) -> int:
    return text.count(needle)


def line_of(text: str, needle: str) -> int:
    index = text.find(needle)
    if index < 0:
        raise AssertionError("text not found: " + needle)
    return text.count("\n", 0, index) + 1
