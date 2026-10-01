import { afterEach, describe, expect, it, vi } from 'vitest';
import { cachedSearchClient } from '../core/cachedSearchClient.ts';
import { SEARCH_REUSE_TTL_MS } from '../constants.ts';
import type { CardMatch, SearchClient } from '../types.ts';

const e1 = [1, 0, 0, 0];
const e1b = [0.995, 0.1, 0, 0]; // cos ≈ 0.995 with e1
const e2 = [0, 1, 0, 0];

function fakeInner() {
  const calls: number[][] = [];
  let fail = false;
  const inner: SearchClient = {
    async searchCards(embedding) {
      calls.push(embedding);
      if (fail) throw new Error('down');
      return [{ cardId: 'x', gameName: 'mtg', score: 0.9 } as CardMatch];
    },
    async searchSetSymbol() {
      return null;
    },
  };
  return { inner, calls, setFail: (v: boolean) => (fail = v) };
}

afterEach(() => vi.restoreAllMocks());

describe('cachedSearchClient', () => {
  it('reuses the answer for a near-identical embedding and the same games', async () => {
    const { inner, calls } = fakeInner();
    const c = cachedSearchClient(inner);
    await c.searchCards(e1, ['mtg']);
    await c.searchCards(e1b, ['mtg']);
    expect(calls).toHaveLength(1);
    await c.searchCards(e1, ['lorcana']);
    await c.searchCards(e2, ['mtg']);
    expect(calls).toHaveLength(3);
  });

  it('forgets an answer after the TTL', async () => {
    const { inner, calls } = fakeInner();
    const c = cachedSearchClient(inner);
    let now = 0;
    vi.spyOn(performance, 'now').mockImplementation(() => now);
    await c.searchCards(e1, ['mtg']);
    now = SEARCH_REUSE_TTL_MS + 1;
    await c.searchCards(e1, ['mtg']);
    expect(calls).toHaveLength(2);
  });

  it('does not replay a failed request', async () => {
    const { inner, calls, setFail } = fakeInner();
    const c = cachedSearchClient(inner);
    setFail(true);
    await expect(c.searchCards(e1, ['mtg'])).rejects.toThrow('down');
    setFail(false);
    await c.searchCards(e1, ['mtg']);
    expect(calls).toHaveLength(2);
  });
});
