/** Search memoization by embedding similarity: a card that stays in view
 *  produces near-identical embeddings frame after frame, and the remote
 *  round trip costs tens of milliseconds. Different cards sit far apart
 *  (cosine ≈ 0.7 between printings), so the threshold never merges them.
 *  In-flight requests are shared, so a burst of frames on a new card issues
 *  one request. */

import {
  SEARCH_REUSE_MIN_COS,
  SEARCH_REUSE_TTL_MS,
  SEARCH_REUSE_CAPACITY,
} from '../constants.ts';
import type { CardMatch, SearchClient, SetSymbolMatch } from '../types.ts';

interface Entry<T> {
  embedding: number[];
  key: string;
  at: number;
  value: Promise<T>;
}

function dot(a: readonly number[], b: readonly number[]): number {
  let s = 0;
  for (let i = 0; i < a.length; i++) s += a[i] * b[i];
  return s;
}

class ReuseCache<T> {
  private entries: Entry<T>[] = [];

  /** Embeddings are L2-normalized, so cosine similarity is the dot. */
  get(embedding: number[], key: string, now: number): Promise<T> | undefined {
    this.entries = this.entries.filter(
      (e) => now - e.at <= SEARCH_REUSE_TTL_MS,
    );
    const hit = this.entries.find(
      (e) =>
        e.key === key && dot(e.embedding, embedding) >= SEARCH_REUSE_MIN_COS,
    );
    if (hit) hit.at = now; // still in view: keep it alive
    return hit?.value;
  }

  put(embedding: number[], key: string, now: number, value: Promise<T>): void {
    this.entries.push({ embedding, key, at: now, value });
    if (this.entries.length > SEARCH_REUSE_CAPACITY) this.entries.shift();
    // A failed request must not be replayed from the cache.
    value.catch(() => {
      this.entries = this.entries.filter((e) => e.value !== value);
    });
  }
}

/** WebCardScanner applies this by default (`reuseSearchResults`). */
export function cachedSearchClient(inner: SearchClient): SearchClient {
  const cards = new ReuseCache<CardMatch[]>();
  const symbols = new ReuseCache<SetSymbolMatch | null>();
  return {
    searchCards(embedding, games) {
      const key = [...games].sort().join(',');
      const now = performance.now();
      const hit = cards.get(embedding, key, now);
      if (hit) return hit;

      const request = inner.searchCards(embedding, games);
      cards.put(embedding, key, now, request);
      return request;
    },
    searchSetSymbol(embedding) {
      const now = performance.now();
      const hit = symbols.get(embedding, '', now);
      if (hit) return hit;

      const request = inner.searchSetSymbol(embedding);
      symbols.put(embedding, '', now, request);
      return request;
    },
  };
}
