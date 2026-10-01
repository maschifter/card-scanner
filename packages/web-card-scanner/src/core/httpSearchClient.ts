/** SearchClient over the reference HTTP API (apps/web-example/server). */

import type { CardMatch, SearchClient, SetSymbolMatch } from '../types.ts';

export function httpSearchClient(apiBaseUrl: string): SearchClient {
  const base = apiBaseUrl.replace(/\/$/, '');
  const post = async <T>(path: string, body: unknown): Promise<T> => {
    const res = await fetch(`${base}${path}`, {
      method: 'POST',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify(body),
    });
    if (!res.ok) throw new Error(`${path} ${res.status}`);
    return (await res.json()) as T;
  };
  return {
    async searchCards(embedding, games) {
      const { matches } = await post<{ matches: CardMatch[] }>(
        '/search-cards',
        { embedding, games },
      );
      return matches;
    },
    async searchSetSymbol(embedding) {
      const { match } = await post<{ match: SetSymbolMatch | null }>(
        '/search-set-symbol',
        { embedding },
      );
      return match;
    },
  };
}
