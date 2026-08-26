// Card id -> display name, generated from the card_nexus CDN.
// Regenerate when the CDN card data changes; see CARD_NAMES_SOURCE below.
//
// Stored as two parallel arrays rather than one object: Hermes caps a single
// object at 196,607 properties and this index is larger than that.
export const CARD_NAMES_SOURCE =
  'https://mobile-data-cdn.cardnexus.com/manifest.json';

let index: Map<string, string> | null = null;

function load(): Map<string, string> {
  if (!index) {
    const data = require('../assets/card-names.json') as {
      ids: string[];
      names: string[];
    };
    index = new Map();
    for (let i = 0; i < data.ids.length; i++) {
      index.set(data.ids[i], data.names[i]);
    }
  }
  return index;
}

/** Display name for a card id, or undefined if it isn't in the index. */
export function cardName(cardId?: string): string | undefined {
  if (!cardId) return undefined;
  return load().get(cardId);
}

/** Name when known, otherwise the raw id so something is always shown. */
export function cardLabel(cardId?: string): string {
  return cardName(cardId) ?? cardId ?? '';
}
