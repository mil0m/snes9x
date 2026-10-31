#!/usr/bin/env python3
"""Sums `corrupt-smoke ... --learn [held] [from]` outputs, e.g. one comparison split over
several processes:

    tests/headless/learn_summary.py fe4-0.txt fe4-10.txt
"""
import re
import sys

LINE = re.compile(r"^seed (\d+)\s+(\d+) crashes plain,\s+(\d+) avoiding learned,\s+(\d+) avoiding random"
                  r"(?:; writes (\d+) / (\d+) / (\d+))?")

rows = []
for path in sys.argv[1:]:
    with open(path) as f:
        for line in f:
            m = LINE.match(line)
            if m:
                rows.append([int(g) if g else 0 for g in m.groups()])
            elif line.startswith("  ") and "blocks seen" in line and path == sys.argv[1]:
                print(line.rstrip())

if not rows:
    sys.exit("no 'seed ...' lines found")
plain, learned, random = (sum(r[i] for r in rows) for i in (1, 2, 3))
writes = [sum(r[i] for r in rows) for i in (4, 5, 6)]
print(f"{len(rows)} held-out seeds")
print(f"  crashes  plain {plain}, avoiding learned {learned}, avoiding random {random}")
if any(writes):
    print(f"  writes   plain {writes[0]}, avoiding learned {writes[1]}, avoiding random {writes[2]}")
for name, i in (("learned", 2), ("random", 3)):
    better = sum(r[i] < r[1] for r in rows)
    worse = sum(r[i] > r[1] for r in rows)
    print(f"  {name:8} fewer crashes on {better} seeds, more on {worse}, the same on {len(rows) - better - worse}")
