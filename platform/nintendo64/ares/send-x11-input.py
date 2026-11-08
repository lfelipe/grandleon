#!/usr/bin/env python3
# SPDX-License-Identifier: MIT

"""Drive ares through its ordinary X11 keyboard input.

The production ROM remains byte-for-byte unchanged.  This helper is an
external controller for review runs, analogous to somebody pressing a pad.
Set ARES_INPUT_SEQUENCE to comma-separated actions, for example:

    wait:3,press:Return,wait:1,shot:title.png

Supported actions are wait (seconds), press (X11 keysym), keydown, keyup and
shot (a filename written below ARES_OUT_DIR).
"""

from __future__ import annotations

import ctypes
import os
from pathlib import Path
import subprocess
import time


DISPLAY = os.environ.get("DISPLAY", ":99").encode()
SEQUENCE = os.environ.get("ARES_INPUT_SEQUENCE", "")
OUT_DIR = Path(os.environ.get("ARES_OUT_DIR", "/out"))


def _libraries() -> tuple[ctypes.CDLL, ctypes.CDLL, ctypes.c_void_p]:
    x11 = ctypes.CDLL("libX11.so.6")
    xtst = ctypes.CDLL("libXtst.so.6")
    x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
    x11.XOpenDisplay.restype = ctypes.c_void_p
    x11.XStringToKeysym.argtypes = [ctypes.c_char_p]
    x11.XStringToKeysym.restype = ctypes.c_ulong
    x11.XKeysymToKeycode.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
    x11.XKeysymToKeycode.restype = ctypes.c_uint
    x11.XFlush.argtypes = [ctypes.c_void_p]
    x11.XCloseDisplay.argtypes = [ctypes.c_void_p]
    xtst.XTestFakeKeyEvent.argtypes = [
        ctypes.c_void_p,
        ctypes.c_uint,
        ctypes.c_int,
        ctypes.c_ulong,
    ]
    xtst.XTestFakeKeyEvent.restype = ctypes.c_int
    display = x11.XOpenDisplay(DISPLAY)
    if not display:
        raise RuntimeError(f"cannot open X display {DISPLAY.decode()}")
    return x11, xtst, display


def main() -> None:
    if not SEQUENCE:
        return
    x11, xtst, display = _libraries()

    def key(name: str, down: bool) -> None:
        symbol = x11.XStringToKeysym(name.encode())
        code = x11.XKeysymToKeycode(display, symbol)
        if not symbol or not code:
            raise ValueError(f"unknown X11 key: {name}")
        if not xtst.XTestFakeKeyEvent(display, code, int(down), 0):
            raise RuntimeError(f"XTest rejected {name}")
        x11.XFlush(display)

    try:
        for raw in SEQUENCE.split(","):
            action, separator, value = raw.strip().partition(":")
            if not separator:
                raise ValueError(f"action needs a value: {raw}")
            if action == "wait":
                time.sleep(float(value))
            elif action == "press":
                key(value, True)
                time.sleep(0.12)
                key(value, False)
            elif action == "keydown":
                key(value, True)
            elif action == "keyup":
                key(value, False)
            elif action == "shot":
                path = OUT_DIR / Path(value).name
                subprocess.run(
                    ["import", "-display", DISPLAY.decode(), "-window", "root", str(path)],
                    check=True,
                )
            else:
                raise ValueError(f"unsupported action: {action}")
    finally:
        x11.XCloseDisplay(display)


if __name__ == "__main__":
    main()
