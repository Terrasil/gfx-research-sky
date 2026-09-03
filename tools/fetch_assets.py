#!/usr/bin/env python3

from __future__ import annotations

import argparse
import sys
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "assets" / "models"

ASSET_SOURCES = {
    "dragon.obj": [
        "https://raw.githubusercontent.com/Domodhoro/Stanford-Dragon/main/dragon.obj",
        "https://raw.githubusercontent.com/adrianderstroff/pbr/master/assets/models/dragon.obj",
    ],
    "suzanne.obj": [
        "https://raw.githubusercontent.com/alecjacobson/common-3d-test-models/master/data/suzanne.obj",
    ],
    "teapot.obj": [
        "https://raw.githubusercontent.com/jaz303/utah-teapot/master/teapot.obj",
    ],
}


def valid_obj(path: Path) -> bool:
    if not path.is_file() or path.stat().st_size < 128:
        return False
    with path.open("rb") as stream:
        head = stream.read(1024 * 1024).lower()
    return b"<html" not in head and (b"\nv " in b"\n" + head or b"\nvn " in b"\n" + head or b"\nf " in b"\n" + head)


def download(url: str, target: Path) -> None:
    request = urllib.request.Request(
        url,
        headers={"User-Agent": "gfx-research-sky asset fetcher/1.0"},
    )
    temporary = target.with_suffix(target.suffix + ".part")
    try:
        with urllib.request.urlopen(request, timeout=90) as response, temporary.open("wb") as output:
            while chunk := response.read(1024 * 1024):
                output.write(chunk)
        if not valid_obj(temporary):
            raise RuntimeError("downloaded data is not a valid OBJ file")
        temporary.replace(target)
    finally:
        temporary.unlink(missing_ok=True)


def fetch_one(name: str, force: bool) -> bool:
    target = ASSETS / name
    if valid_obj(target) and not force:
        print(f"[ok] {name} already exists ({target.stat().st_size / 1024 / 1024:.2f} MiB)")
        return True

    for url in ASSET_SOURCES[name]:
        print(f"[fetch] {name}\n        {url}")
        try:
            download(url, target)
            print(f"[ok] {name} ({target.stat().st_size / 1024 / 1024:.2f} MiB)")
            return True
        except (OSError, RuntimeError, urllib.error.URLError) as error:
            print(f"[warn] {error}")

    print(f"[error] could not download {name}")
    return False


def main() -> int:
    parser = argparse.ArgumentParser(description="Download the classic research meshes used by gfx-research-sky.")
    parser.add_argument("--force", action="store_true", help="replace files that already exist")
    args = parser.parse_args()

    ASSETS.mkdir(parents=True, exist_ok=True)
    success = True
    for name in ASSET_SOURCES:
        success = fetch_one(name, args.force) and success
    if not success:
        print("\nAt least one asset could not be downloaded. See assets/SOURCES.md for manual sources.")
        return 1

    print("\nAll research assets are ready.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
