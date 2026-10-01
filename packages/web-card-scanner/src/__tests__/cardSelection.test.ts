import { describe, expect, it } from 'vitest';
import { StickyTracker } from '../core/cardSelection.ts';

const W = 1000;
const H = 1000;
const at = (cx: number, cy: number, id: string) => ({
  id,
  quad: null,
  box: { x1: cx - 50, y1: cy - 75, x2: cx + 50, y2: cy + 75 },
});

describe('StickyTracker', () => {
  it('picks the centre-most card and then sticks to it', () => {
    const t = new StickyTracker();
    const a = at(560, 500, 'a');
    expect(t.select([a, at(900, 500, 'b')], W, H, 0).map((c) => c.id)).toEqual([
      'a',
    ]);
    // b moves a little closer to centre than a: not a clear re-aim.
    const out = t.select([at(540, 500, 'b'), a], W, H, 100);
    expect(out.map((c) => c.id)).toEqual(['a']);
  });

  it('switches when a rival is clearly closer to the centre', () => {
    const t = new StickyTracker();
    t.select([at(500, 500, 'a'), at(800, 500, 'b')], W, H, 0);
    const out = t.select([at(700, 500, 'a'), at(510, 500, 'b')], W, H, 100);
    expect(out.map((c) => c.id)).toEqual(['b']);
  });

  it('reports nothing while the tracked card is missed, unless a rival is dead-centre', () => {
    const t = new StickyTracker();
    t.select([at(500, 500, 'a'), at(800, 500, 'b')], W, H, 0);
    expect(t.select([at(800, 500, 'b'), at(200, 500, 'c')], W, H, 100)).toEqual(
      [],
    );
    const out = t.select([at(505, 505, 'c'), at(800, 500, 'b')], W, H, 200);
    expect(out.map((c) => c.id)).toEqual(['c']);
  });

  it('forgets the tracked card after the timeout', () => {
    const t = new StickyTracker();
    t.select([at(500, 500, 'a'), at(800, 500, 'b')], W, H, 0);
    const out = t.select([at(800, 500, 'b'), at(300, 500, 'c')], W, H, 600);
    expect(out.map((c) => c.id)).toEqual(['c']);
  });
});
