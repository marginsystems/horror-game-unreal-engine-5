#!/usr/bin/env python3
"""Downloads the surface textures the building is painted with, from Poly Haven.

    python3 Scripts/fetch_textures.py <output folder>

Every texture on Poly Haven is CC0 (public domain), so the imported copies can live in this
repo. Scripts/make-assets.sh runs this and then imports the results into Content/Textures.
"""
import json
import os
import sys
import urllib.request

# role -> (Poly Haven asset, resolution)
PICKS = {
    "wall": ("plaster_brick_01", "2k"),
    "floor": ("concrete_floor_worn_001", "1k"),
}
MAPS = (("Diffuse", "diff"), ("nor_dx", "nor"), ("Rough", "rough"))
AGENT = {"User-Agent": "horror-game-asset-fetch"}


def get(url):
    return urllib.request.urlopen(urllib.request.Request(url, headers=AGENT), timeout=120).read()


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: fetch_textures.py <output folder>")
    folder = sys.argv[1]
    os.makedirs(folder, exist_ok=True)
    for role, (asset, size) in PICKS.items():
        files = json.loads(get(f"https://api.polyhaven.com/files/{asset}"))
        for kind, short in MAPS:
            path = os.path.join(folder, f"T_{role}_{short}.jpg")
            with open(path, "wb") as out:
                out.write(get(files[kind][size]["jpg"]["url"]))
            print(f"{asset} {kind} {size} -> {os.path.basename(path)}")


if __name__ == "__main__":
    main()
