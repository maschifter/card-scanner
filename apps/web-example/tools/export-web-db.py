#!/usr/bin/env python3
"""Extract card/set-symbol embeddings from the ObjectBox .mdb databases into
flat binaries for the web example's backend.

ObjectBox stores are plain LMDB files holding FlatBuffers entities; lmdb-js
cannot open them (its patched LMDB fork aborts), so the Node server reads
this flat format instead:

  [4s magic b"CDB1"][u32 dims][u32 count][u32 idsJsonLen]
  [idsJson utf8: ["cardId", ...]][count*dims f32 LE embeddings]

Usage:
  python3 apps/web-example/tools/export-web-db.py --assets apps/mobile-example/assets \
      --out apps/web-example/server/data

Needs: lmdb (pip).
"""

import argparse
import json
import struct
from pathlib import Path

import lmdb


def decode_entity(buf: bytes, dims: int):
    """FlatBuffers table with field 1 = string id, field 2 = [float] of `dims`.
    Returns (id, floats-bytes) or None for anything else (metadata, index)."""
    try:
        root = struct.unpack_from("<I", buf, 0)[0]
        vt = root - struct.unpack_from("<i", buf, root)[0]
        vt_size = struct.unpack_from("<H", buf, vt)[0]

        def field(i):
            slot = 4 + 2 * i
            if slot + 2 > vt_size:
                return 0
            off = struct.unpack_from("<H", buf, vt + slot)[0]
            return root + off if off else 0

        s_pos, v_pos = field(1), field(2)
        if not s_pos or not v_pos:
            return None
        so = s_pos + struct.unpack_from("<I", buf, s_pos)[0]
        s_len = struct.unpack_from("<I", buf, so)[0]
        vo = v_pos + struct.unpack_from("<I", buf, v_pos)[0]
        v_len = struct.unpack_from("<I", buf, vo)[0]
        if v_len != dims or vo + 4 + 4 * dims > len(buf):
            return None
        return buf[so + 4 : so + 4 + s_len].decode(), buf[vo + 4 : vo + 4 + 4 * dims]
    except (struct.error, UnicodeDecodeError):
        return None


def extract(mdb: Path, out: Path, dims: int) -> int:
    env = lmdb.open(str(mdb), subdir=False, readonly=True, lock=False, max_dbs=64)
    ids, vecs = [], []
    with env.begin() as txn:
        for key, value in txn.cursor():
            if len(key) != 8:
                continue
            entity = decode_entity(value, dims)
            if entity:
                ids.append(entity[0])
                vecs.append(entity[1])
    env.close()
    ids_json = json.dumps(ids).encode()
    with open(out, "wb") as f:
        f.write(struct.pack("<4sIII", b"CDB1", dims, len(ids), len(ids_json)))
        f.write(ids_json)
        for v in vecs:
            f.write(v)
    return len(ids)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--assets", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)

    total = 0
    for mdb in sorted(args.assets.glob("*.mdb")):
        n = extract(mdb, args.out / f"{mdb.stem}.bin", 256)
        total += n
        print(f"{mdb.stem}: {n} cards")
    symbols = args.assets / "mtg" / "mtg-sets.mdb"
    if symbols.exists():
        n = extract(symbols, args.out / "set-symbols.bin", 128)
        print(f"set-symbols: {n} symbols")
    names = args.assets / "card-names.json"
    if names.exists():
        (args.out / "card-names.json").write_bytes(names.read_bytes())
    print(f"total: {total} cards -> {args.out}")


if __name__ == "__main__":
    main()
