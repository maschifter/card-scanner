#!/usr/bin/env python3
"""Ground truth for the offline benchmark, lifted from the mobile example.

benchmarkImages.ts is the one place the expected card ids live; this turns it
into the JSON the desktop harness reads. benchmark-all.sh runs it into a temp
file on every run, so the manifest is never stored; for a manual run:

  tools/gen-benchmark-manifest.py > /tmp/benchmark-manifest.json

Keys are "<game>/<file>", which is how the harness identifies an image from
the directory it was handed.
"""
import json
import pathlib
import re
import sys

TS = pathlib.Path(__file__).resolve().parents[2] / "mobile-example/utils/benchmarkImages.ts"
entry = re.compile(
    r"game:\s*'([^']+)'.*?cardIds:\s*\[([^\]]*)\].*?require\('([^']+)'\)", re.S)

manifest = {}
for game, ids, path in entry.findall(TS.read_text()):
    key = "%s/%s" % (game, path.rsplit("/", 1)[-1])
    manifest[key] = {"game": game, "cardIds": re.findall(r"'([^']+)'", ids)}

if not manifest:
    sys.exit("no entries parsed from %s" % TS)
json.dump(manifest, sys.stdout, indent=1, sort_keys=True)
sys.stdout.write("\n")
