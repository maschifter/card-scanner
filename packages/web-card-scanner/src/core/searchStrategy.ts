/** Candidate-game routing — SearchStrategy.extractTopGames: walk the sorted
 *  class scores, stop below the confidence floor, cap at 3 mapped classes /
 *  4 database names. A merged class (pokemon, dbs) expands to several slugs. */

import { MAX_CANDIDATE_CLASSES, MAX_CANDIDATE_GAMES } from '../constants.ts';
import type { GameClassEntry } from '../types.ts';

export function extractTopGames(
  mapping: readonly GameClassEntry[],
  classScores: Float32Array,
  minGameConfidence: number,
): string[] {
  const order = [...classScores.keys()].sort(
    (a, b) => classScores[b] - classScores[a],
  );
  const games: string[] = [];
  let mapped = 0;
  for (const idx of order) {
    if (
      classScores[idx] < minGameConfidence ||
      mapped >= MAX_CANDIDATE_CLASSES ||
      games.length >= MAX_CANDIDATE_GAMES
    ) {
      break;
    }
    const entry = mapping.find((m) => m.labelIndex === idx);
    if (!entry) continue;
    mapped++;
    for (const slug of entry.gameSlugs) {
      if (!games.includes(slug) && games.length < MAX_CANDIDATE_GAMES)
        games.push(slug);
    }
  }
  return games;
}
