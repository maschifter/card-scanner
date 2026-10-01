/** Scans its slice of one game's shared matrix (workerData) and posts the
 *  local top-k back. */

import { parentPort, workerData } from 'node:worker_threads';
import { topKRows } from './topk.mjs';

const dbs = workerData; // name → { matrix, dims }
parentPort.on('message', ({ id, game, q, k, start, end }) => {
  const { matrix, dims } = dbs[game];
  parentPort.postMessage({
    id,
    hits: topKRows(matrix, dims, q, k, start, end),
  });
});
