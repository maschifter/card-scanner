import { describe, expect, it } from 'vitest';
import { extractTopGames } from '../core/searchStrategy.ts';

const mapping = [
  { labelIndex: 0, label: 'mtg', gameSlugs: ['mtg'] },
  { labelIndex: 1, label: 'pokemon', gameSlugs: ['pokemon', 'pokemon-japan'] },
  { labelIndex: 2, label: 'lorcana', gameSlugs: ['lorcana'] },
  { labelIndex: 3, label: 'fab', gameSlugs: ['fab'] },
  { labelIndex: 4, label: 'swu', gameSlugs: ['swu'] },
];

describe('extractTopGames', () => {
  it('walks classes by score and stops under the floor', () => {
    const scores = new Float32Array([0.2, 0.05, 0.7, 0.3, 0.01]);
    expect(extractTopGames(mapping, scores, 0.1)).toEqual([
      'lorcana',
      'fab',
      'mtg',
    ]);
  });

  it('caps at three classes and four databases, expanding merged classes', () => {
    const scores = new Float32Array([0.9, 0.8, 0.7, 0.6, 0.5]);
    expect(extractTopGames(mapping, scores, 0.1)).toEqual([
      'mtg',
      'pokemon',
      'pokemon-japan',
      'lorcana',
    ]);
  });

  it('skips unmapped classes without consuming a slot', () => {
    const scores = new Float32Array([0.9, 0.1, 0.1, 0.1, 0.1, 0.95]);
    expect(extractTopGames(mapping, scores, 0.5)).toEqual(['mtg']);
  });
});
