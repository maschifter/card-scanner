import { describe, expect, it } from 'vitest';
import { checkInputSize, locate } from '../core/pipeline.ts';
import { letterboxPlacement } from '../inference/gpuPreprocess.ts';

describe('checkInputSize', () => {
  it('accepts positive multiples of 32', () => {
    for (const size of [32, 288, 384, 640]) {
      expect(() => checkInputSize('segmentationInputSize', size)).not.toThrow();
    }
  });

  it('rejects anything else by name', () => {
    for (const size of [0, -288, 300, 288.5, NaN]) {
      expect(() => checkInputSize('segmentationInputSize', size)).toThrow(
        /segmentationInputSize must be a positive multiple of 32/,
      );
    }
  });
});

describe('locate', () => {
  it('keeps the detection box when a quad exists', () => {
    // A 10×14-cell card in a 20×20 mask; the detection box is larger.
    const mask = new Uint8Array(20 * 20);
    for (let y = 3; y < 17; y++)
      for (let x = 5; x < 15; x++) mask[y * 20 + x] = 255;
    const det = {
      x1: 0,
      y1: 0,
      x2: 80,
      y2: 80,
      score: 0.9,
      classId: 0,
      classScores: new Float32Array(1),
      mask,
      maskX: 0,
      maskY: 0,
      maskW: 20,
      maskH: 20,
    };
    const found = locate(det, letterboxPlacement(384, 384, 384), 4);
    expect(found.quad).not.toBeNull();
    expect(found.box).toEqual({ x1: 0, y1: 0, x2: 80, y2: 80 });
  });
});
