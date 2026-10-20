import type { ScannedCard } from './types';

export function LanguageTag({ card }: { card: ScannedCard }) {
  return card.language ? (
    <span className="tag">{card.language.code.toUpperCase()}</span>
  ) : null;
}
