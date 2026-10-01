/** Card-search backend for the web example: the HTTP face of search.mjs.
 *  Owns the card databases so clients never download them.
 *
 *  GET  /api/health             → { games: { <game>: cardCount }, setSymbols }
 *  POST /api/search-cards       { embedding: number[256], games: string[] }
 *                               → { matches: CardMatch[] }
 *  POST /api/search-set-symbol  { embedding: number[128] }
 *                               → { match: SetSymbolMatch | null }
 *
 *  Env:
 *    WEB_SCANNER_DATA_DIR  dir with <game>.bin / set-symbols.bin /
 *                          card-names.json (default: ./data next to this file)
 *    PORT                  default 8787
 */

import { createServer } from 'node:http';
import { existsSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { openSearch } from './search.mjs';

const here = dirname(fileURLToPath(import.meta.url));
const DATA_DIR = process.env.WEB_SCANNER_DATA_DIR ?? join(here, 'data');
const PORT = Number(process.env.PORT ?? 8787);

if (!existsSync(DATA_DIR)) {
  console.error(
    `${DATA_DIR} not found — run:\n` +
      '  python3 apps/web-example/tools/export-web-db.py ' +
      '--assets apps/mobile-example/assets ' +
      '--out apps/web-example/server/data',
  );
  process.exit(1);
}

console.log(`loading databases from ${DATA_DIR} …`);
const t0 = Date.now();
const search = openSearch(DATA_DIR);
const cardCount = [...search.games.values()].reduce(
  (s, db) => s + db.ids.length,
  0,
);
console.log(
  `  ${search.games.size} games, ${cardCount} cards, ` +
    `${search.setSymbolCount} set symbols, ${search.cardNameCount} card names`,
);
console.log(`ready in ${Date.now() - t0} ms`);

function json(res, status, body) {
  res.writeHead(status, {
    'content-type': 'application/json',
    'access-control-allow-origin': '*',
    'access-control-allow-headers': 'content-type',
  });
  res.end(JSON.stringify(body));
}

async function handle(req, res) {
  if (req.method === 'OPTIONS') return json(res, 204, {});
  const url = new URL(req.url, 'http://x');
  if (req.method === 'GET' && url.pathname === '/api/health') {
    return json(res, 200, {
      games: Object.fromEntries(
        [...search.games].map(([g, db]) => [g, db.ids.length]),
      ),
      setSymbols: search.setSymbolCount,
    });
  }
  if (req.method !== 'POST') return json(res, 404, { error: 'not found' });
  let body = '';
  for await (const chunk of req) body += chunk;
  let parsed;
  try {
    parsed = JSON.parse(body);
  } catch {
    return json(res, 400, { error: 'invalid JSON' });
  }
  if (typeof parsed !== 'object' || parsed === null) {
    return json(res, 400, { error: 'expected a JSON object' });
  }
  if (url.pathname === '/api/search-cards') {
    const { embedding, games } = parsed;
    if (
      !Array.isArray(embedding) ||
      embedding.length !== 256 ||
      !Array.isArray(games)
    ) {
      return json(res, 400, {
        error: 'expected { embedding: number[256], games: string[] }',
      });
    }
    return json(res, 200, {
      matches: await search.searchCards(embedding, games.map(String)),
    });
  }
  if (url.pathname === '/api/search-set-symbol') {
    const { embedding } = parsed;
    if (!Array.isArray(embedding) || embedding.length !== 128) {
      return json(res, 400, { error: 'expected { embedding: number[128] }' });
    }
    return json(res, 200, { match: await search.searchSetSymbol(embedding) });
  }
  return json(res, 404, { error: 'not found' });
}

// One bad or aborted request must not take the whole backend down. Bad
// bodies are answered 400 above, so anything thrown here is a server bug: 500.
const server = createServer((req, res) =>
  handle(req, res).catch((e) => {
    if (res.destroyed) return; // the client went away mid-request
    console.error(e);
    if (!res.headersSent) json(res, 500, { error: String(e?.message ?? e) });
  }),
);

server.listen(PORT, () =>
  console.log(`card-search backend on http://localhost:${PORT}`),
);
