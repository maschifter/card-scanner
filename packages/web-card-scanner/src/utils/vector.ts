/** In-place L2 normalization — F.normalize(p=2) with torch's eps. */
export function l2Normalize(v: Float32Array): Float32Array {
  let s = 0;
  for (const x of v) s += x * x;
  const inv = 1 / Math.max(Math.sqrt(s), 1e-12);
  for (let i = 0; i < v.length; i++) v[i] *= inv;
  return v;
}

export function softmaxTop(logits: Float32Array): {
  index: number;
  probability: number;
} {
  const max = Math.max(...logits);
  const exps = [...logits].map((v) => Math.exp(v - max));
  const sum = exps.reduce((a, b) => a + b, 0);
  let best = 0;
  for (let i = 1; i < exps.length; i++) if (exps[i] > exps[best]) best = i;
  return { index: best, probability: exps[best] / sum };
}
