#!/usr/bin/env python3
# SPDX-License-Identifier: MIT

"""Drive ares through its ordinary X11 keyboard input.

The production ROM remains byte-for-byte unchanged.  This helper is an
external controller for review runs, analogous to somebody pressing a pad.
Set ARES_INPUT_SEQUENCE to comma-separated actions, for example:

    wait:3,press:Return,wait:1,shot:title.png

Supported actions are wait (seconds), press (X11 keysym), keydown, keyup,
shot (a filename written below ARES_OUT_DIR), film-start (ffmpeg capture while later
controller actions continue; requires ffmpeg in the capture image), and film (seconds with no input,
timestamped screen captures below ARES_OUT_DIR/idle-film).
"""

from __future__ import annotations

import ctypes
from concurrent.futures import ThreadPoolExecutor
import math
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

    background_videos = []
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
            elif action == "film-start":
                seconds = float(value)
                if not math.isfinite(seconds) or not 0 < seconds <= 60:
                    raise ValueError("film duration must be between zero and 60 seconds")
                log = (OUT_DIR / "controller-film.log").open("wb")
                process = subprocess.Popen([
                    "ffmpeg", "-hide_banner", "-loglevel", "warning",
                    "-f", "x11grab", "-framerate", "30", "-video_size", "1024x768",
                    "-i", DISPLAY.decode(), "-t", str(seconds),
                    "-vf", "crop=768:576:108:81,scale=640:480:flags=neighbor",
                    "-c:v", "libx264", "-preset", "ultrafast", "-crf", "16",
                    "-pix_fmt", "yuv420p", "-movflags", "+faststart",
                    "-y", str(OUT_DIR / "controller-film.mp4"),
                ], stdout=log, stderr=subprocess.STDOUT)
                background_videos.append((process, log))
            elif action == "film":
                seconds = float(value)
                if not math.isfinite(seconds) or not 0 < seconds <= 60:
                    raise ValueError("film duration must be between zero and 60 seconds")
                folder = OUT_DIR / "idle-film"
                folder.mkdir(exist_ok=False)
                end = time.monotonic() + seconds

                def capture() -> None:
                    while time.monotonic() < end:
                        path = folder / f"{time.time_ns()}.png"
                        subprocess.run(
                            ["import", "-display", DISPLAY.decode(), "-window", "root", str(path)],
                            check=True,
                        )

                # Several independent readbacks improve capture cadence.
                # They send no controller events and never advance the game.
                with ThreadPoolExecutor(max_workers=6) as workers:
                    for result in workers.map(lambda _: capture(), range(6)):
                        pass
            else:
                raise ValueError(f"unsupported action: {action}")
    finally:
        for process, log in background_videos:
            try:
                if process.wait() != 0:
                    raise RuntimeError("ffmpeg controller capture failed; inspect controller-film.log")
            finally:
                log.close()
        x11.XCloseDisplay(display)


if __name__ == "__main__":
    main()
