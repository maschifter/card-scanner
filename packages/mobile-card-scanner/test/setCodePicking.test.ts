// Run with `yarn test` (node --test; Node strips the types itself).
import assert from 'node:assert/strict';
import { test } from 'node:test';
import {
  catalogKey,
  pickPrinting,
  printedForms,
  setCodeLogProb,
} from '../src/setCodePicking.ts';

// The manifest's set_code_reader labels, as a read carries them.
const CLASS_NAMES = ['blank', ...'-0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ'];
const CLASSES = CLASS_NAMES.length;

/** A read that saw `text` cleanly: one sure character per step with blanks
 *  between, padded with blanks to 64 steps. `blur` moves that much
 *  probability from each character to `swap` (its look-alike). */
function read(text: string, blur = 0, swap: Record<string, string> = {}) {
  const steps = 64;
  const probs = new Float64Array(steps * CLASSES).fill(1e-6);
  const path = ['', '', ...[...text].flatMap((c) => [c, ''])];
  for (let t = 0; t < steps; t++) {
    const c = path[t] ?? '';
    const k = c ? CLASS_NAMES.indexOf(c) : 0;
    probs[t * CLASSES + k] = c && swap[c] ? 1 - blur : 1;
    if (c && swap[c]) {
      probs[t * CLASSES + CLASS_NAMES.indexOf(swap[c])] = blur;
    }
  }
  return { probs, steps, classes: CLASSES, classNames: CLASS_NAMES };
}

test('printed forms add the language infixes the catalog drops', () => {
  assert.ok(printedForms('YGLD-A22').includes('YGLD-ENA22'));
  assert.ok(printedForms('LOB-070').includes('LOB-070'));
  assert.ok(printedForms('ygld-a22').includes('YGLD-ENA22'));
  assert.equal(catalogKey('IOC-25TH-102'), catalogKey('IOC-102'));
  assert.equal(catalogKey('BODE-001'), catalogKey('B0DE-00I'));
});

test('a clean read scores its own code highest', () => {
  const r = read('RDS-EN034');
  assert.ok(setCodeLogProb(r, 'RDS-EN034') > setCodeLogProb(r, 'RDS-EN035'));
  assert.equal(setCodeLogProb(r, 'RDS-EN03a'), -Infinity);
  assert.equal(setCodeLogProb(r, ''), -Infinity);
  assert.equal(setCodeLogProb({ ...r, steps: 0 }, 'RDS-EN034'), -Infinity);
});

test('lowercase print numbers still match', () => {
  const result = pickPrinting(read('RDS-EN034'), ['rds-034', 'RDS-035']);
  assert.equal(result.pick, 'rds-034');
});

test('picks the printing the read shows among the candidates', () => {
  const result = pickPrinting(read('RATE-EN002'), [
    'RATE-002',
    'RA01-002',
    'OP12-002',
  ]);
  assert.equal(result.pick, 'RATE-002');
  assert.equal(result.ranking[0].printed, 'RATE-EN002');
  assert.ok(result.margin >= 6);
});

test('a misread character still picks the right printing', () => {
  // The reader leans 40% toward J for the I in CHIM.
  const result = pickPrinting(read('CHIM-EN060', 0.4, { I: 'J' }), [
    'CHIM-060',
    'CHJM-061',
  ]);
  assert.equal(result.pick, 'CHIM-060');
});

test('editions sharing one printed code do not block the pick', () => {
  const result = pickPrinting(read('LOB-070'), [
    'LOB-070',
    'LOB-E070',
    'SDJ-001',
  ]);
  assert.ok(result.pick === 'LOB-070' || result.pick === 'LOB-E070');
  assert.ok(result.sharedBy.length >= 1);
});

test('stays undecided when the read cannot tell two codes apart', () => {
  // Half 4, half 9: LOB-004 and LOB-009 are equally likely.
  const result = pickPrinting(read('LOB-004', 0.5, { '4': '9' }), [
    'LOB-004',
    'LOB-009',
  ]);
  assert.equal(result.pick, null);
});

test('an empty read picks nothing', () => {
  const result = pickPrinting(read(''), ['LOB-070', 'MRD-003']);
  assert.equal(result.pick, null);
});
