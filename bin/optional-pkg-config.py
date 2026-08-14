#!/usr/bin/env python3
"""Print optional pkg-config flags without failing the PlatformIO build."""

from __future__ import annotations

import shutil
import subprocess
import sys


def main() -> int:
    executable = shutil.which("pkg-config") or shutil.which("pkgconf")
    if executable is None:
        return 0

    result = subprocess.run(
        [executable, *sys.argv[1:]],
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode == 0 and result.stdout:
        sys.stdout.write(result.stdout)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
