/** CardLanguageModel::classify. Null under LANGUAGE_MIN_CONF. */

import {
  LANGUAGE_MIN_CONF,
  LANGUAGE_MIN_TRADITIONAL,
  LANGUAGE_TOP_ROWS,
} from '../constants.ts';
import { LANGUAGE_CODES } from '../models/cardLanguage.ts';
import type { CardLanguageInfo } from '../types.ts';
import type { Runner } from '../inference/runner.ts';
import { argMax } from '../utils/array.ts';

// TODO: Deduplication possible, requires change in softmaxTop (vector.ts) and decode.ts
const sigmoid = (x: number) => 1 / (1 + Math.exp(-x));

function softmax(logits: readonly number[]): number[] {
  const max = Math.max(...logits);
  const exps = logits.map((v) => Math.exp(v - max));
  const sum = exps.reduce((a, b) => a + b, 0);
  return exps.map((v) => v / sum);
}

/** The graph's tail: the row answers of the LANGUAGE_TOP_ROWS rows most on
 *  text, averaged by how much they are. `logits` is [channel][row]. */
export function languageProbs(logits: Float32Array): {
  probs: number[];
  traditional: number;
} {
  const classes = LANGUAGE_CODES.length;
  const rows = logits.length / (classes + 2);
  const at = (channel: number, row: number) => logits[channel * rows + row];
  const weights = Array.from({ length: rows }, (_, r) =>
    sigmoid(at(classes, r)),
  );
  const floor = [...weights].sort((a, b) => b - a)[LANGUAGE_TOP_ROWS - 1];

  const probs = LANGUAGE_CODES.map(() => 0);
  let traditional = 0;
  let total = 0;
  weights.forEach((weight, r) => {
    if (weight < floor) return;
    softmax(LANGUAGE_CODES.map((_, c) => at(c, r))).forEach((p, c) => {
      probs[c] += p * weight;
    });
    traditional += sigmoid(at(classes + 1, r)) * weight;
    total += weight;
  });
  total = Math.max(total, 1e-4);
  return {
    probs: probs.map((p) => p / total),
    traditional: traditional / total,
  };
}

export async function classifyLanguage(
  dewarp: ImageData,
  classifier: Runner,
): Promise<CardLanguageInfo | null> {
  const [logits] = await classifier.run(dewarp);
  const { probs, traditional } = languageProbs(logits.data);
  const index = argMax([...probs.keys()], (i) => probs[i]);
  if (probs[index] < LANGUAGE_MIN_CONF) return null;
  const code = LANGUAGE_CODES[index];
  const script = traditional >= LANGUAGE_MIN_TRADITIONAL ? 'zht' : 'zhs';
  return { code: code === 'zh' ? script : code, score: probs[index] };
}
