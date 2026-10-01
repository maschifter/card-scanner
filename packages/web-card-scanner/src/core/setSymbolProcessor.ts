import {
  SYMBOL_CROP_PADDING,
  SYMBOL_EMBED_SIZE,
  YOLO_INPUT_SIZE,
  YOLO_STRIDES,
} from '../constants.ts';
import type { SearchClient, SetSymbolMatch } from '../types.ts';
import type { Runner } from '../inference/runner.ts';
import {
  cropPlacement,
  letterboxPlacement,
  type Letterbox,
  type Rect,
} from '../inference/gpuPreprocess.ts';
import type { Box } from '../utils/box.ts';
import { decodeBoxes, rawLevels } from '../utils/decode.ts';
import { l2Normalize } from '../utils/vector.ts';

export async function matchSetSymbol(
  dewarp: ImageData,
  detector: Runner,
  embedder: Runner,
  search: SearchClient,
  detectionThreshold: number,
): Promise<SetSymbolMatch | null> {
  const letterbox = letterboxPlacement(
    dewarp.width,
    dewarp.height,
    YOLO_INPUT_SIZE,
  );
  const outs = await detector.run(dewarp, letterbox.placement);
  const [top] = decodeBoxes(rawLevels(outs, YOLO_STRIDES), {
    confThreshold: detectionThreshold,
  });
  if (!top) return null;

  const crop = symbolCrop(top, letterbox, dewarp);
  if (crop.w < 2 || crop.h < 2) return null;

  const [emb] = await embedder.run(
    dewarp,
    cropPlacement(crop, SYMBOL_EMBED_SIZE),
  );
  return search.searchSetSymbol([...l2Normalize(emb.data)]);
}

/** The detected box in dewarp pixels, grown around its centre and clipped
 *  to the image — the embedder was trained on 10%-padded crops. */
function symbolCrop(box: Box, letterbox: Letterbox, image: ImageData): Rect {
  const p1 = letterbox.toSource(box.x1, box.y1);
  const p2 = letterbox.toSource(box.x2, box.y2);
  const cx = (p1.x + p2.x) / 2;
  const cy = (p1.y + p2.y) / 2;
  const halfW = ((p2.x - p1.x) * (1 + SYMBOL_CROP_PADDING)) / 2;
  const halfH = ((p2.y - p1.y) * (1 + SYMBOL_CROP_PADDING)) / 2;

  const x1 = Math.max(0, Math.floor(cx - halfW));
  const y1 = Math.max(0, Math.floor(cy - halfH));
  const x2 = Math.min(image.width, Math.ceil(cx + halfW));
  const y2 = Math.min(image.height, Math.ceil(cy + halfH));
  return { x: x1, y: y1, w: x2 - x1, h: y2 - y1 };
}
