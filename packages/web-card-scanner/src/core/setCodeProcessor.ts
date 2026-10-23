/** SetCodeProcessor (core/SetCodeProcessor.cpp): reads the printed set code
 *  (Yu-Gi-Oh!: YGLD-ENA22) off the dewarped card, which is already the
 *  640 px tall card every search box assumes.
 *
 *  Tries the regular box, the regular box re-centered on the text box's top
 *  border, then Link, then Pendulum, and stops at the first code-shaped read
 *  the box did not clip. Crops, gray and the per-crop normalization run on
 *  the CPU; the model runs on the GPU. */

import {
  SET_CODE_ABOVE_TEXT_BOX,
  SET_CODE_BOXES,
  SET_CODE_FIRST_CLEAR_STEP,
  SET_CODE_INPUT_HEIGHT,
  SET_CODE_INPUT_WIDTH,
  SET_CODE_LAST_CLEAR_STEP,
  TEXT_BOX_BAND_BOTTOM,
  TEXT_BOX_BAND_TOP,
  TEXT_BOX_DOUBLE_BORDER,
  TEXT_BOX_LINE_COVERAGE,
  TEXT_BOX_LINE_KERNEL,
} from '../constants.ts';
import type { SetCodeInfo, SetCodeLayout } from '../types.ts';
import type { Runner } from '../inference/runner.ts';
import type { Rect } from '../inference/gpuPreprocess.ts';
import {
  normalizeSetCodeCrop,
  SET_CODE_ALPHABET,
  SET_CODE_CLASS_NAMES,
} from '../models/setCodeReader.ts';
import { softmax } from '../utils/vector.ts';

/** "YGLD-ENA22", "ME-10": one dash, a 2-5 character prefix, a 2-7 character
 *  rest holding a digit. */
export function looksLikeCode(text: string): boolean {
  return /^[^-]{2,5}-(?=[^-]*\d)[^-]{2,7}$/.test(text);
}

/** A greedy read, as SetCodeInfo carries it, plus where its text starts and
 *  ends. */
type Read = Pick<
  SetCodeInfo,
  'text' | 'confidence' | 'probs' | 'steps' | 'classes'
> & { first: number; last: number };

/** One pass over the logits: softmax per step, the greedy CTC decode, where
 *  the text starts and ends, and the confidence (geometric mean of the
 *  per-step best probability). */
export function decode(logits: Float32Array, classes: number): Read {
  const steps = logits.length / classes;
  const probs = new Float32Array(logits.length);
  let text = '';
  let prev = 0;
  let first = -1;
  let last = -1;
  let sumLog = 0;
  for (let t = 0; t < steps; t++) {
    const row = softmax(logits.subarray(t * classes, (t + 1) * classes));
    probs.set(row, t * classes);
    let best = 0;
    for (let k = 1; k < classes; k++) if (row[k] > row[best]) best = k;
    sumLog += Math.log(Math.max(row[best], 1e-9));
    if (best !== 0) {
      if (first < 0) first = t;
      last = t;
      if (best !== prev) text += SET_CODE_ALPHABET[best - 1];
    }
    prev = best;
  }
  const confidence = steps ? Math.exp(sumLog / steps) : 0;
  return { text, confidence, probs, steps, classes, first, last };
}

/** SetCodeProcessor::isBetter: a code-shaped read beats any other text, any
 *  text beats an empty read, and within that the more confident one wins. An
 *  empty read is confident (every step is a sure blank), so confidence only
 *  breaks ties. */
export function isBetter(
  read: Pick<Read, 'text' | 'confidence'>,
  than: Pick<Read, 'text' | 'confidence'>,
): boolean {
  const tier = (r: Pick<Read, 'text'>) =>
    looksLikeCode(r.text) ? 2 : r.text ? 1 : 0;
  const a = tier(read);
  const b = tier(than);
  return a !== b ? a > b : read.confidence > than.confidence;
}

function looksClipped(r: Read, minLastStep: number): boolean {
  return (
    r.first >= 0 &&
    (r.first < SET_CODE_FIRST_CLEAR_STEP ||
      r.last > SET_CODE_LAST_CLEAR_STEP ||
      r.last < minLastStep)
  );
}

/** cvRound: round half to even, as lrint does. */
function cvRound(v: number): number {
  const r = Math.round(v);
  return r - v === 0.5 && r % 2 !== 0 ? r - 1 : r;
}

/** Gray of an 8-bit RGB pixel: 0.299 R + 0.587 G + 0.114 B in OpenCV's
 *  15-bit fixed point, which matches cv::cvtColor(RGB2GRAY) on every color. */
function grayOf(r: number, g: number, b: number): number {
  return (r * 9798 + g * 19235 + b * 3735 + 16384) >> 15;
}

/** cvRound(fraction * size) in float32, as the native code computes it with
 *  float fractions. */
function scaled(fraction: number, size: number): number {
  return cvRound(Math.fround(Math.fround(fraction) * size));
}

/** cv::Rect of a fractional cv::Rect2f box, clipped to the image. */
function toPixels(box: Rect, width: number, height: number): Rect {
  const x = scaled(box.x, width);
  const y = scaled(box.y, height);
  const x0 = Math.max(0, x);
  const y0 = Math.max(0, y);
  const x1 = Math.min(width, x + scaled(box.w, width));
  const y1 = Math.min(height, y + scaled(box.h, height));
  return { x: x0, y: y0, w: Math.max(0, x1 - x0), h: Math.max(0, y1 - y0) };
}

/** cv::resize INTER_LINEAR's taps and 11-bit weights for one axis: the
 *  source index of each output pixel's first tap (unclamped), and the two
 *  weights. Computed in float32 like OpenCV. */
function linearTaps(src: number, dst: number) {
  const f = Math.fround;
  const scale = src / dst;
  const index = new Int32Array(dst);
  const w0 = new Int32Array(dst);
  const w1 = new Int32Array(dst);
  for (let d = 0; d < dst; d++) {
    const at = f((d + 0.5) * scale - 0.5);
    const s = Math.floor(at);
    const frac = f(at - s);
    index[d] = s;
    w0[d] = cvRound(f(f(1 - frac) * 2048));
    w1[d] = cvRound(f(frac * 2048));
  }
  return { index, w0, w1 };
}

/** The crop resized to the model input and turned gray in 0-1, bit-exact
 *  with cv::resize(INTER_LINEAR) then cv::cvtColor(RGB2GRAY) on 8-bit
 *  pixels, as SetCodeReader::read does. */
export function grayCrop(
  card: ImageData,
  rect: Rect,
): Float32Array<ArrayBuffer> {
  const W = SET_CODE_INPUT_WIDTH;
  const H = SET_CODE_INPUT_HEIGHT;
  const { data, width } = card;
  const xs = linearTaps(rect.w, W);
  const ys = linearTaps(rect.h, H);

  // Horizontal pass per source row: past the left or right edge OpenCV takes
  // the edge pixel at full weight.
  const rows = new Int32Array(rect.h * W * 3);
  for (let y = 0; y < rect.h; y++) {
    const line = ((rect.y + y) * width + rect.x) * 4;
    for (let d = 0; d < W; d++) {
      let s = xs.index[d];
      let a0 = xs.w0[d];
      let a1 = xs.w1[d];
      if (s < 0 || s >= rect.w - 1) {
        s = Math.min(Math.max(s, 0), rect.w - 1);
        a0 = 2048;
        a1 = 0;
      }
      const s1 = Math.min(s + 1, rect.w - 1);
      for (let c = 0; c < 3; c++) {
        rows[(y * W + d) * 3 + c] =
          data[line + s * 4 + c] * a0 + data[line + s1 * 4 + c] * a1;
      }
    }
  }

  // Vertical pass: the row index clamps at the edges but the weights stay,
  // rounded the way OpenCV's vectorized 8-bit path rounds.
  const out = new Float32Array(W * H);
  const pixel = [0, 0, 0];
  for (let d = 0; d < H; d++) {
    const s = ys.index[d];
    const r0 = Math.min(Math.max(s, 0), rect.h - 1) * W;
    const r1 = Math.min(Math.max(s + 1, 0), rect.h - 1) * W;
    const b0 = ys.w0[d];
    const b1 = ys.w1[d];
    for (let x = 0; x < W; x++) {
      for (let c = 0; c < 3; c++) {
        const v0 = rows[(r0 + x) * 3 + c] >> 4;
        const v1 = rows[(r1 + x) * 3 + c] >> 4;
        const v = (((b0 * v0) >> 16) + ((b1 * v1) >> 16) + 2) >> 2;
        pixel[c] = Math.min(Math.max(v, 0), 255);
      }
      out[d * W + x] = grayOf(pixel[0], pixel[1], pixel[2]) / 255;
    }
  }
  return out;
}

/** SetCodeProcessor::textBoxTop: the text box's top border, the strongest
 *  long horizontal line in the band where it sits, as a fraction of the card
 *  height; -1 when there is none. */
export function textBoxTop(card: ImageData): number {
  const { width: w, height: h, data } = card;
  const top = scaled(TEXT_BOX_BAND_TOP, h);
  const bottom = scaled(TEXT_BOX_BAND_BOTTOM, h);
  const rows = bottom - top;
  if (rows < 4 || w < 16) return -1;

  // Gray band plus a row on each side: OpenCV blurs the band as a view into
  // the whole card, so its neighbors come from the card, and reflect-101
  // applies only at the card's own edges. Blur 3x3 Gaussian (1 2 1 / 4 per
  // axis), rounded back to 8 bits.
  const reflect = (i: number, n: number) =>
    i < 0 ? -i : i >= n ? 2 * n - 2 - i : i;
  const gray = new Float32Array((rows + 2) * w);
  for (let y = 0; y < rows + 2; y++) {
    const source = reflect(top + y - 1, h);
    for (let x = 0; x < w; x++) {
      const i = (source * w + x) * 4;
      gray[y * w + x] = grayOf(data[i], data[i + 1], data[i + 2]);
    }
  }
  const blurX = new Float32Array((rows + 2) * w);
  for (let y = 0; y < rows + 2; y++) {
    for (let x = 0; x < w; x++) {
      blurX[y * w + x] =
        (gray[y * w + reflect(x - 1, w)] +
          2 * gray[y * w + x] +
          gray[y * w + reflect(x + 1, w)]) /
        4;
    }
  }
  const blurred = new Float32Array(rows * w);
  for (let y = 0; y < rows; y++) {
    for (let x = 0; x < w; x++) {
      blurred[y * w + x] = Math.round(
        (blurX[y * w + x] +
          2 * blurX[(y + 1) * w + x] +
          blurX[(y + 2) * w + x]) /
          4,
      );
    }
  }

  // |Sobel y|: (1 2 1) across, (-1 0 1) down.
  const dy = new Float32Array(rows * w);
  for (let y = 0; y < rows; y++) {
    const up = reflect(y - 1, rows) * w;
    const down = reflect(y + 1, rows) * w;
    for (let x = 0; x < w; x++) {
      const l = reflect(x - 1, w);
      const r = reflect(x + 1, w);
      dy[y * w + x] = Math.abs(
        blurred[down + l] +
          2 * blurred[down + x] +
          blurred[down + r] -
          blurred[up + l] -
          2 * blurred[up + x] -
          blurred[up + r],
      );
    }
  }

  // Keep the strongest 10% of horizontal edges. Every value is a whole
  // number (8-bit pixels through integer taps), so a histogram finds the
  // 90th percentile without a sort.
  const counts = new Uint32Array(4 * 255 + 1);
  for (const v of dy) counts[v]++;
  let cut = 0;
  for (let seen = 0; cut < counts.length; cut++) {
    seen += counts[cut];
    if (seen > Math.floor(dy.length * 0.9)) break;
  }
  const edges = new Uint8Array(rows * w);
  for (let i = 0; i < dy.length; i++) edges[i] = dy[i] > cut ? 1 : 0;

  // Opening with a 1-row kernel 30% of the card wide: only edges running
  // straight that far survive. Outside the image counts as neither. Each
  // window's count comes from a running sum along the row.
  const k = Math.max(3, scaled(TEXT_BOX_LINE_KERNEL, w));
  const anchor = Math.floor(k / 2);
  const sums = new Uint32Array(w + 1);
  const slide = (src: Uint8Array, erode: boolean) => {
    const out = new Uint8Array(src.length);
    for (let y = 0; y < rows; y++) {
      for (let x = 0; x < w; x++) sums[x + 1] = sums[x] + src[y * w + x];
      for (let x = 0; x < w; x++) {
        const from = Math.max(0, x - anchor);
        const to = Math.min(w, x - anchor + k);
        const on = sums[to] - sums[from];
        out[y * w + x] = (erode ? on === to - from : on > 0) ? 1 : 0;
      }
    }
    return out;
  };
  const opened = slide(slide(edges, true), false);

  // Rows covered well enough, merged into lines (a border is 2-4 rows).
  const lines: { first: number; last: number; strength: number }[] = [];
  for (let y = 0; y < rows; y++) {
    let on = 0;
    for (let x = 0; x < w; x++) on += opened[y * w + x];
    const coverage = on / w;
    if (coverage <= TEXT_BOX_LINE_COVERAGE) continue;
    const prev = lines[lines.length - 1];
    if (prev && y - prev.last <= 2) {
      prev.last = y;
      prev.strength = Math.max(prev.strength, coverage);
    } else {
      lines.push({ first: y, last: y, strength: coverage });
    }
  }
  if (!lines.length) return -1;
  const center = (l: { first: number; last: number }) =>
    (top + (l.first + l.last) / 2) / h;
  let best = 0;
  for (let i = 1; i < lines.length; i++) {
    if (lines[i].strength > lines[best].strength) best = i;
  }
  // A double border reads as two lines about 1% apart: take the upper one.
  while (
    best > 0 &&
    center(lines[best]) - center(lines[best - 1]) < TEXT_BOX_DOUBLE_BORDER
  ) {
    best--;
  }
  return center(lines[best]);
}

interface LayoutBox {
  layout: SetCodeLayout;
  box: Rect;
  minLastStep: number;
}

const layoutBox = (layout: keyof typeof SET_CODE_BOXES): LayoutBox => {
  const { minLastStep, ...box } = SET_CODE_BOXES[layout];
  return { layout, box, minLastStep };
};

type Kept = Read & { layout: SetCodeLayout; box: Rect };

/** Reads the set code; `reader` is the SetCodeReader runner. */
export async function readSetCode(
  card: ImageData,
  reader: Runner,
): Promise<SetCodeInfo | null> {
  // Until a read is found, the best so far (isBetter).
  const state: { kept: Kept | null; found: boolean } = {
    kept: null,
    found: false,
  };

  const attempt = async ({ layout, box, minLastStep }: LayoutBox) => {
    const rect = toPixels(box, card.width, card.height);
    if (rect.w < 8 || rect.h < 8) return;
    const [out] = await reader.runInput(
      normalizeSetCodeCrop(grayCrop(card, rect)),
    );
    const read = decode(out.data, out.dims[out.dims.length - 1]);
    const code = looksLikeCode(read.text) && !looksClipped(read, minLastStep);
    if (code || !state.kept || isBetter(read, state.kept)) {
      state.kept = { ...read, layout, box };
      state.found = code;
    }
  };

  const regular = layoutBox('regular');
  await attempt(regular);
  if (!state.found) {
    const line = textBoxTop(card);
    if (line > 0) {
      const y = line - SET_CODE_ABOVE_TEXT_BOX - regular.box.h / 2;
      await attempt({
        ...regular,
        layout: 'anchored',
        box: { ...regular.box, y },
      });
    }
  }
  if (!state.found) await attempt(layoutBox('link'));
  if (!state.found) await attempt(layoutBox('pendulum'));

  if (!state.kept) return null;
  const { first: _first, last: _last, box, ...read } = state.kept;
  return {
    ...read,
    region: [box.x, box.y, box.w, box.h],
    classNames: SET_CODE_CLASS_NAMES,
  };
}
