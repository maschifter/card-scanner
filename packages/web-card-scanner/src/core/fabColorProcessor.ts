/** Null under FAB_COLOR_MIN_CONF: a badly cropped indicator yields a
 *  confident-looking three-way guess. */

import {
  COLOR_BAR_SIZE,
  FAB_COLOR_MIN_CONF,
  FAB_INDICATOR_H,
  FAB_INDICATOR_W,
} from '../constants.ts';
import { COLOR_BAR_LABELS } from '../models/colorBar.ts';
import type { FabColor } from '../types.ts';
import type { Runner } from '../inference/runner.ts';
import { cropPlacement } from '../inference/gpuPreprocess.ts';
import { softmaxTop } from '../utils/vector.ts';

export async function classifyFabColor(
  dewarp: ImageData,
  classifier: Runner,
): Promise<FabColor | null> {
  const w = Math.max(1, Math.round(dewarp.width * FAB_INDICATOR_W));
  const h = Math.max(1, Math.round(dewarp.height * FAB_INDICATOR_H));
  const [logits] = await classifier.run(
    dewarp,
    cropPlacement({ x: 0, y: 0, w, h }, COLOR_BAR_SIZE),
  );
  const { index, probability } = softmaxTop(logits.data);
  if (probability < FAB_COLOR_MIN_CONF) return null;
  return { color: COLOR_BAR_LABELS[index], score: probability };
}
