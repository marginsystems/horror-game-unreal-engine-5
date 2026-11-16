#!/usr/bin/env python3
"""Checks the text maps in Source/HorrorGame/HGLevel.cpp.

Every map must be a closed 21 x 15 rectangle with one player, one monster and one
exit, and the player must be able to walk to every fuse, every item and the exit.
"""
import pathlib
import re
import sys

SOURCE = pathlib.Path(__file__).resolve().parent.parent / "Source/HorrorGame/HGLevel.cpp"
WIDTH, HEIGHT = 21, 15


def load_maps():
    text = SOURCE.read_text()
    block = text.split("// MAPS-BEGIN")[1].split("// MAPS-END")[0]
    maps = {}
    for name, body in re.findall(r"(\w+)Map\[\] = \{(.*?)\};", block, re.S):
        maps[name] = re.findall(r'TEXT\("([^"]*)"\)', body)
    return maps


def check(name, rows):
    errors = []
    if len(rows) != HEIGHT:
        errors.append(f"has {len(rows)} rows, wants {HEIGHT}")
    for y, row in enumerate(rows):
        if len(row) != WIDTH:
            errors.append(f"row {y} is {len(row)} wide, wants {WIDTH}")
    if errors:
        return errors

    cells = {(x, y): c for y, row in enumerate(rows) for x, c in enumerate(row)}
    for (x, y), c in cells.items():
        if (x in (0, WIDTH - 1) or y in (0, HEIGHT - 1)) and c != "#":
            errors.append(f"border open at {x},{y}")
    for mark in "PME":
        count = sum(1 for c in cells.values() if c == mark)
        if count != 1:
            errors.append(f"has {count} '{mark}', wants 1")
    if errors:
        return errors

    start = next(p for p, c in cells.items() if c == "P")
    seen, stack = {start}, [start]
    while stack:
        x, y = stack.pop()
        for step in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            n = (x + step[0], y + step[1])
            if n not in seen and cells.get(n, "#") not in "#B":
                seen.add(n)
                stack.append(n)
    for p, c in cells.items():
        if c not in "#B" and p not in seen:
            errors.append(f"'{c}' at {p[0]},{p[1]} cannot be reached")
    fuses = sum(1 for c in cells.values() if c == "F")
    if fuses != 5:
        errors.append(f"has {fuses} fuses, wants 5")
    return errors


def main():
    failed = False
    for name, rows in load_maps().items():
        errors = check(name, rows)
        print(f"{name}: {'ok' if not errors else 'FAILED'}")
        for e in errors:
            print(f"  {e}")
        failed = failed or bool(errors)
    sys.exit(1 if failed else 0)


main()
