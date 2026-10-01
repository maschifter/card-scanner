/** Brute-force dot-product top-k over the flat files from
 *  apps/web-example/tools/export-web-db.py — the same rescore ObjectBoxDB applies after its
 *  HNSW pass — plus the native SearchStrategy accept gate.
 *  ponytail: linear scan (~27M mul-adds for the 106k-card MTG db, tens of ms);
 *  swap in a vector index when query latency matters. */

import { readdirSync, readFileSync, existsSync } from 'node:fs';
import { availableParallelism } from 'node:os';
import { Worker } from 'node:worker_threads';
import { join, basename } from 'node:path';
import { topKRows } from './topk.mjs';

// Search thresholds from the training repo (card_nexus/config.py) — the
// two-signal accept gate: a strong absolute score, or a moderate one that
// stands clear of its runner-up.
export const MATCH_CONF = 0.6;
export const MATCH_CONF_LOW = 0.45;
export const MATCH_MARGIN = 0.05;
export const SEARCH_CANDIDATES = 100;
export const MAX_MATCHES = 5;
export const SET_SYMBOL_THRESHOLD = 0.6;

/** SearchStrategy.filterToBestGame with the card_nexus two-signal gate. Only
 *  the winning game's matches survive: reranking across games would let an
 *  mtg symbol promote a lorcana card. */
export function acceptMatches(sorted) {
  const top = sorted[0];
  if (!top) return [];
  const margin = sorted.length > 1 ? top.score - sorted[1].score : top.score;
  const floor =
    top.score >= MATCH_CONF
      ? MATCH_CONF
      : top.score >= MATCH_CONF_LOW && margin >= MATCH_MARGIN
        ? MATCH_CONF_LOW
        : null;
  if (floor === null) return [];
  return sorted
    .filter((m) => m.gameName === top.gameName && m.score >= floor)
    .slice(0, MAX_MATCHES);
}

/** Read one export-web-db.py file: CDB1 header, ids JSON, f32 matrix. */
function loadDb(path) {
  const buf = readFileSync(path);
  if (buf.toString('ascii', 0, 4) !== 'CDB1')
    throw new Error(`${path}: bad magic`);
  const dims = buf.readUInt32LE(4);
  const count = buf.readUInt32LE(8);
  const idsLen = buf.readUInt32LE(12);
  const ids = JSON.parse(buf.toString('utf8', 16, 16 + idsLen));
  const start = buf.byteOffset + 16 + idsLen;
  if (dims % 4 !== 0)
    throw new Error(`${path}: dims ${dims} not a multiple of 4`);
  // Shared memory so the worker pool scans the same bytes without copies.
  const matrix = new Float32Array(new SharedArrayBuffer(count * dims * 4));
  matrix.set(new Float32Array(buf.buffer, start, count * dims));
  return { name: basename(path, '.bin'), ids, matrix, dims };
}

export function openSearch(dataDir) {
  const games = new Map();
  let setSymbols = null;
  for (const f of readdirSync(dataDir)) {
    if (!f.endsWith('.bin')) continue;
    const db = loadDb(join(dataDir, f));
    if (db.name === 'set-symbols') setSymbols = db;
    else games.set(db.name, db);
  }
  const cardNames = new Map();
  const namesPath = join(dataDir, 'card-names.json');
  if (existsSync(namesPath)) {
    const { ids, names } = JSON.parse(readFileSync(namesPath, 'utf8'));
    ids.forEach((id, i) => cardNames.set(id, names[i]));
  }

  // The scan is compute-bound scalar JS (~1 multiply-add per ns, so ~20 ms
  // for the 106k-row mtg matrix on one core). worker.mjs instances split the
  // row range and merge their local top-k; the matrices live in
  // SharedArrayBuffers, so the only per-query traffic is the 256-float query
  // and k hits per worker.
  const workers = [];
  const inflight = new Map();
  let nextId = 0;
  const shared = {};
  for (const [name, db] of games)
    shared[name] = { matrix: db.matrix, dims: db.dims };
  for (let w = 0; w < Math.max(1, availableParallelism() - 1); w++) {
    const worker = new Worker(new URL('./worker.mjs', import.meta.url), {
      workerData: shared,
    });
    worker.on('message', ({ id, hits }) => {
      const p = inflight.get(id);
      inflight.delete(id);
      p?.(hits);
    });
    workers.push(worker);
  }

  async function topK(db, query, k) {
    const { ids, matrix, dims } = db;
    const n = ids.length;
    const q = Float32Array.from(query);
    const rowsPer = Math.ceil(n / workers.length);
    // Small tables (and the set-symbol db, which the workers do not hold)
    // are not worth the message round trip.
    const parts =
      n < 8192 || !shared[db.name]
        ? [topKRows(matrix, dims, q, k, 0, n)]
        : await Promise.all(
            workers.map((worker, w) => {
              const start = w * rowsPer;
              const end = Math.min(n, start + rowsPer);
              if (start >= end) return [];
              const id = nextId++;
              return new Promise((resolve) => {
                inflight.set(id, resolve);
                worker.postMessage({ id, game: db.name, q, k, start, end });
              });
            }),
          );
    return parts
      .flat()
      .sort((a, b) => b.score - a.score)
      .slice(0, k)
      .map((h) => ({ cardId: ids[h.i], score: h.score }));
  }

  return {
    games,
    setSymbolCount: setSymbols?.ids.length ?? 0,
    cardNameCount: cardNames.size,

    /** SearchStrategy.searchCard, without its early exit. */
    async searchCards(embedding, requestedGames) {
      const perGame = await Promise.all(
        requestedGames
          .filter((name) => games.has(name))
          .map(async (name) =>
            (await topK(games.get(name), embedding, SEARCH_CANDIDATES)).map(
              (r) => ({ ...r, gameName: name }),
            ),
          ),
      );
      return acceptMatches(
        perGame.flat().sort((a, b) => b.score - a.score),
      ).map((m) => ({ ...m, name: cardNames.get(m.cardId) ?? null }));
    },

    async searchSetSymbol(embedding) {
      if (!setSymbols) return null;
      const [top] = await topK(setSymbols, embedding, 1);
      return top && top.score >= SET_SYMBOL_THRESHOLD
        ? { setCode: top.cardId, score: top.score }
        : null;
    },
  };
}
