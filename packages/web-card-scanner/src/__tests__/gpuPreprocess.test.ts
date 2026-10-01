import { describe, expect, it } from 'vitest';
import { letterboxPlacement } from '../inference/gpuPreprocess.ts';

describe('letterboxPlacement', () => {
  it('pads with whole pixels when the margin is odd', () => {
    // 1080×1350 scales to 307×384, leaving a 77 px margin.
    const lb = letterboxPlacement(1080, 1350, 384);
    expect(lb.placement.dst).toEqual({ x: 38, y: 0, w: 307, h: 384 });
    expect(lb.toSource(38, 0)).toEqual({ x: 0, y: 0 });
  });
});
