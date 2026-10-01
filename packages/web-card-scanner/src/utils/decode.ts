/** Seg decode adapted from @tie/zoo/yolo26 pipeline.ts, extended with the
 *  full per-cell class-score vector — the native SearchStrategy walks the
 *  sorted class confidences (not just the argmax) to pick candidate games,
 *  so the zoo's Detection (top class only) is not enough here. YOLO26 is
 *  end-to-end (one-to-one head): threshold + sort, no NMS. */

import type { ProtoData, RawLevel } from '@tie/zoo/yolo26';
import type { Output } from '../inference/runner.ts';
import {
  CONTAINED_MIN_AREA_FRAC,
  GROUP_BOX_MIN_ASPECT_MISMATCH,
  GROUP_BOX_MIN_AREA_RATIO,
  GROUP_BOX_MIN_CONTAINED_CARDS,
  MAX_DETECTIONS,
} from '../constants.ts';
import {
  area,
  aspectRatioMismatch,
  containedFraction,
  type Box,
} from './box.ts';

export interface CardDetection extends Box {
  score: number;
  classId: number;
  /** All class scores (sigmoid), index = labelIndex of gameClassMapping. */
  classScores: Float32Array;
  /** Soft coverage mask cropped to the (padded) box, proto-grid coords. */
  mask: Uint8Array;
  maskX: number;
  maskY: number;
  maskW: number;
  maskH: number;
}

export interface ScoredBox extends Box {
  score: number;
}

const DEFAULT_MASK_COEFFS = 32;
const BOX_CHANNELS = 4;

const sigmoid = (x: number) => 1 / (1 + Math.exp(-x));

/** A confidence threshold in logit space, so cells compare before sigmoid. */
const logit = (p: number) => Math.log(p / (1 - p));

/** A head level is `[channels, h, w]` row-major: channel `c` of cell `i`. */
const channel = (level: RawLevel, c: number, i: number) =>
  level.data[c * level.h * level.w + i];

/** The box of cell `i`: distances from the cell centre, in input pixels. */
function cellBox(level: RawLevel, i: number): Box {
  const cx = (i % level.w) + 0.5;
  const cy = Math.floor(i / level.w) + 0.5;
  return {
    x1: (cx - channel(level, 0, i)) * level.stride,
    y1: (cy - channel(level, 1, i)) * level.stride,
    x2: (cx + channel(level, 2, i)) * level.stride,
    y2: (cy + channel(level, 3, i)) * level.stride,
  };
}

interface Hit {
  level: RawLevel;
  cell: number;
  det: CardDetection;
}

export function decodeCards(
  levels: readonly RawLevel[],
  proto: ProtoData,
  opts: {
    numClasses: number;
    confThreshold: number;
    /** Input pixels added around each box before cropping its mask. */
    maskPadding: number;
    maxDet?: number;
    numMasks?: number;
  },
): CardDetection[] {
  const {
    numClasses,
    confThreshold,
    maskPadding,
    maxDet = MAX_DETECTIONS,
    numMasks = DEFAULT_MASK_COEFFS,
  } = opts;
  const threshold = logit(confThreshold);

  // Pass 1: every cell whose best class clears the threshold.
  const hits: Hit[] = [];
  for (const level of levels) {
    const cells = level.h * level.w;
    for (let i = 0; i < cells; i++) {
      let bestLogit = -Infinity;
      let bestClass = 0;
      for (let c = 0; c < numClasses; c++) {
        const v = channel(level, BOX_CHANNELS + c, i);
        if (v > bestLogit) {
          bestLogit = v;
          bestClass = c;
        }
      }
      if (bestLogit < threshold) continue;

      const classScores = new Float32Array(numClasses);
      for (let c = 0; c < numClasses; c++) {
        classScores[c] = sigmoid(channel(level, BOX_CHANNELS + c, i));
      }
      hits.push({
        level,
        cell: i,
        det: {
          ...cellBox(level, i),
          score: sigmoid(bestLogit),
          classId: bestClass,
          classScores,
          mask: new Uint8Array(0),
          maskX: 0,
          maskY: 0,
          maskW: 0,
          maskH: 0,
        },
      });
    }
  }
  hits.sort((a, b) => b.det.score - a.det.score);

  // Pass 2: render the mask of each kept hit from its coefficients.
  const first = levels[0];
  const protoStride = (first.stride * first.w) / proto.w;
  return hits.slice(0, maxDet).map(({ level, cell, det }) => {
    const coeffs = new Float32Array(numMasks);
    for (let c = 0; c < numMasks; c++) {
      coeffs[c] = channel(level, BOX_CHANNELS + numClasses + c, cell);
    }
    return {
      ...det,
      ...renderMask(det, coeffs, proto, protoStride, maskPadding),
    };
  });
}

/** Mask cells of `box` (padded, clipped to the proto grid) as 0–255 coverage. */
function renderMask(
  box: Box,
  coeffs: Float32Array,
  proto: ProtoData,
  protoStride: number,
  pad: number,
): Pick<CardDetection, 'mask' | 'maskX' | 'maskY' | 'maskW' | 'maskH'> {
  // The native decoder pads the box before cropping the mask so a card edge
  // just outside the box keeps its mask cells.
  const clampX = (v: number) => Math.min(proto.w, Math.max(0, v));
  const clampY = (v: number) => Math.min(proto.h, Math.max(0, v));
  const x1 = clampX(Math.floor((box.x1 - pad) / protoStride));
  const y1 = clampY(Math.floor((box.y1 - pad) / protoStride));
  const x2 = Math.max(x1, clampX(Math.ceil((box.x2 + pad) / protoStride)));
  const y2 = Math.max(y1, clampY(Math.ceil((box.y2 + pad) / protoStride)));
  const maskW = x2 - x1;
  const maskH = y2 - y1;

  const planeSize = proto.h * proto.w;
  const mask = new Uint8Array(maskW * maskH);
  for (let y = 0; y < maskH; y++) {
    for (let x = 0; x < maskW; x++) {
      const p = (y1 + y) * proto.w + (x1 + x);
      let v = 0;
      for (let c = 0; c < coeffs.length; c++) {
        v += coeffs[c] * proto.data[c * planeSize + p];
      }
      mask[y * maskW + x] = Math.round(255 * sigmoid(v));
    }
  }
  return { mask, maskX: x1, maskY: y1, maskW, maskH };
}

export function decodeBoxes(
  levels: readonly RawLevel[],
  opts: { confThreshold: number; maxDet?: number },
): ScoredBox[] {
  const { confThreshold, maxDet = 8 } = opts;
  const threshold = logit(confThreshold);

  const out: ScoredBox[] = [];
  for (const level of levels) {
    const cells = level.h * level.w;
    for (let i = 0; i < cells; i++) {
      const score = channel(level, BOX_CHANNELS, i);
      if (score < threshold) continue;
      out.push({ ...cellBox(level, i), score: sigmoid(score) });
    }
  }
  return out.sort((a, b) => b.score - a.score).slice(0, maxDet);
}

export function rawLevels(
  outs: readonly Output[],
  strides: readonly number[],
): RawLevel[] {
  return strides.map((stride, i) => ({
    data: outs[i].data,
    h: outs[i].dims[1],
    w: outs[i].dims[2],
    stride,
  }));
}

export function protoPlane(out: Output): ProtoData {
  return { data: out.data, h: out.dims[1], w: out.dims[2] };
}

/** Drop "scene" boxes — YoloSegmentationModel.cpp dropGroupBoxes. A box that
 *  wraps two or more other detections, or one much smaller card of a very
 *  different aspect, is the table or a binder page, not a card. */
export function dropGroupBoxes<T extends Box>(boxes: readonly T[]): T[] {
  if (boxes.length < 2) return [...boxes];

  const dropped = boxes.map(() => false);
  boxes.forEach((container, i) => {
    if (dropped[i]) return;

    let contained = 0;
    let last = -1;
    boxes.forEach((b, j) => {
      if (j === i || dropped[j]) return;
      if (containedFraction(b, container) >= CONTAINED_MIN_AREA_FRAC) {
        contained++;
        last = j;
      }
    });

    if (contained >= GROUP_BOX_MIN_CONTAINED_CARDS) {
      dropped[i] = true;
      return;
    }
    if (contained === 1) {
      const inner = boxes[last];
      const muchLarger =
        area(container) >= GROUP_BOX_MIN_AREA_RATIO * area(inner);
      const differentAspect =
        aspectRatioMismatch(container, inner) >= GROUP_BOX_MIN_ASPECT_MISMATCH;
      if (muchLarger && differentAspect) dropped[i] = true;
    }
  });
  return boxes.filter((_, i) => !dropped[i]);
}
