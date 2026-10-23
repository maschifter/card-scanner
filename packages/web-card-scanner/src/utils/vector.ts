/** In-place L2 normalization — F.normalize(p=2) with torch's eps. */
export function l2Normalize(v: Float32Array): Float32Array {
  let s = 0;
  for (const x of v) s += x * x;
  const inv = 1 / Math.max(Math.sqrt(s), 1e-12);
  for (let i = 0; i < v.length; i++) v[i] *= inv;
  return v;
}

/** Softmax over one row of logits, shifted by the max for stability. */
export function softmax(logits: ArrayLike<number>): Float32Array {
  let max = -Infinity;
  for (let i = 0; i < logits.length; i++) max = Math.max(max, logits[i]);
  const out = new Float32Array(logits.length);
  let sum = 0;
  for (let i = 0; i < logits.length; i++) {
    out[i] = Math.exp(logits[i] - max);
    sum += out[i];
  }
  for (let i = 0; i < out.length; i++) out[i] /= sum;
  return out;
}

export function softmaxTop(logits: Float32Array): {
  index: number;
  probability: number;
} {
  const probs = softmax(logits);
  let best = 0;
  for (let i = 1; i < probs.length; i++) if (probs[i] > probs[best]) best = i;
  return { index: best, probability: probs[best] };
}
