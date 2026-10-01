import { test } from 'node:test';
import assert from 'node:assert/strict';
import { acceptMatches } from './search.mjs';

const m = (gameName, score, cardId = `${gameName}-${score}`) => ({
  cardId,
  gameName,
  score,
});

test('accepts a strong top-1 and keeps only its game above the floor', () => {
  const out = acceptMatches([
    m('mtg', 0.8),
    m('lorcana', 0.75),
    m('mtg', 0.7),
    m('mtg', 0.55),
  ]);
  assert.deepEqual(
    out.map((x) => x.score),
    [0.8, 0.7],
  );
});

test('accepts a moderate top-1 only with a clear margin', () => {
  assert.equal(acceptMatches([m('mtg', 0.5), m('mtg', 0.48)]).length, 0);
  assert.deepEqual(
    acceptMatches([m('mtg', 0.5), m('mtg', 0.44)]).map((x) => x.score),
    [0.5],
  );
});

test('rejects below the low floor and caps at five', () => {
  assert.equal(acceptMatches([m('mtg', 0.4)]).length, 0);
  assert.equal(acceptMatches([]).length, 0);
  const many = Array.from({ length: 8 }, (_, i) => m('mtg', 0.9 - i * 0.01));
  assert.equal(acceptMatches(many).length, 5);
});
