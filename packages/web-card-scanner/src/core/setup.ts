import tgpu from 'typegpu';
import {
  createOpfsCache,
  fromSafetensors,
  initTie,
  type Value,
  type WeightCache,
} from '@tie/core';
import type { TgpuRoot } from 'typegpu';
import { loadYolo26Weights, Yolo26Model } from '@tie/zoo/yolo26';
import {
  CARD_EMBED_SIZE,
  COLOR_BAR_SIZE,
  NUM_GAME_CLASSES,
  SLOW_GPU_MS,
  SLOW_GPU_TEST_BUDGET_MS,
  SYMBOL_EMBED_SIZE,
  YOLO_INPUT_SIZE,
} from '../constants.ts';
import type { GameClassEntry, ScannerOptions } from '../types.ts';
import { CardRecognitionModel } from '../models/cardRecognition.ts';
import { SetSymbolRecognitionModel } from '../models/setSymbolRecognition.ts';
import { ColorBarModel } from '../models/colorBar.ts';
import { Runner } from '../inference/runner.ts';

export interface Runners {
  segmentation: Runner;
  cardRecognition: Runner;
  setSymbolDetection: Runner;
  setSymbolRecognition: Runner;
  colorBar: Runner;
}

type Progress = ScannerOptions['onProgress'];

export async function fetchGameClassMapping(
  base: string,
): Promise<GameClassEntry[]> {
  const res = await fetch(
    `${base}/CardSegmentationModel/gameClassMapping.json`,
  );
  if (!res.ok) throw new Error(`gameClassMapping.json: ${res.status}`);
  return (await res.json()) as GameClassEntry[];
}

/** Every model runs its conv stack hwc4+f16, so the device needs shader-f16
 *  (Module.half() throws a clear error without it). Uncaptured errors and
 *  device loss are asynchronous, so they go to `onGpuError`. */
export async function createDevice(
  onGpuError: ScannerOptions['onGpuError'],
): Promise<TgpuRoot> {
  const root = await tgpu.init({
    device: {
      optionalFeatures: ['shader-f16', 'timestamp-query', 'subgroups'],
    },
  });
  initTie(root); // sets the default root for the weight loaders
  root.device.addEventListener('uncapturederror', (e) => {
    const message = (e as GPUUncapturedErrorEvent).error.message;
    console.error('WebGPU:', message);
    onGpuError?.(message);
  });
  void root.device.lost.then((info) =>
    onGpuError?.(`device lost (${info.reason}): ${info.message}`),
  );
  return root;
}

/** Weights are OPFS-cached when available (Safari's support is patchy —
 *  plain fetches otherwise). Progress reports each model dir, then
 *  'shaders' for the warm-up. */
export async function createRunners(
  root: TgpuRoot,
  base: string,
  onProgress: Progress,
  segmentationInputSize: number,
  slowGpuInputSize: number | null,
): Promise<{ runners: Runners; loadTestMs: number | null }> {
  const cache = await createOpfsCache('web-card-scanner').catch(
    () => undefined,
  );
  const progress =
    (stage: string) => (_name: string, done: number, total: number) =>
      onProgress?.(stage, done, total);
  const loadPlain = async <
    M extends CardRecognitionModel | SetSymbolRecognitionModel | ColorBarModel,
  >(
    model: M,
    dir: string,
  ): Promise<M> => {
    const url = `${base}/${dir}/model.safetensors`;
    const sd = await fromSafetensors(url, {
      cache: cache as WeightCache,
      cacheId: url,
    });
    await model.loadStateDict(sd, { root, onProgress: progress(dir) });
    return model;
  };
  const loadYolo = async <T extends 'segment' | 'detect'>(
    task: T,
    numClasses: number,
    dir: string,
  ) => {
    const model = new Yolo26Model('n', { numClasses, task });
    await loadYolo26Weights(model, `${base}/${dir}`, {
      cache,
      onProgress: progress(dir),
    });
    return model;
  };
  const seg = await loadYolo(
    'segment',
    NUM_GAME_CLASSES,
    'CardSegmentationModel',
  );
  const symbolDet = await loadYolo('detect', 1, 'SetSymbolDetectionModel');
  const cardRec = await loadPlain(
    new CardRecognitionModel(),
    'CardRecognitionModel',
  );
  const symbolRec = await loadPlain(
    new SetSymbolRecognitionModel(),
    'SetSymbolRecognitionModel',
  );
  const colorBar = await loadPlain(new ColorBarModel(), 'ColorBarModel');

  const segmentationAt = (size: number) =>
    new Runner(root, [3, size, size], 'scale255', (input: Value) => {
      const { levels, proto } = seg.forward(input);
      return [...levels, proto];
    });
  const runners: Runners = {
    segmentation: segmentationAt(segmentationInputSize),
    cardRecognition: new Runner(
      root,
      [3, CARD_EMBED_SIZE, CARD_EMBED_SIZE],
      'imagenet',
      (input) => [cardRec.forward(input)],
    ),
    setSymbolDetection: new Runner(
      root,
      [3, YOLO_INPUT_SIZE, YOLO_INPUT_SIZE],
      'scale255',
      (input) => [...symbolDet.forward(input)],
    ),
    setSymbolRecognition: new Runner(
      root,
      [3, SYMBOL_EMBED_SIZE, SYMBOL_EMBED_SIZE],
      'imagenet',
      (input) => [symbolRec.forward(input)],
    ),
    colorBar: new Runner(
      root,
      [3, COLOR_BAR_SIZE, COLOR_BAR_SIZE],
      'imagenet',
      (input) => [colorBar.forward(input)],
    ),
  };

  const all = Object.values(runners);
  for (const [i, runner] of all.entries()) {
    onProgress?.('shaders', i, all.length);
    await runner.warmUp();
  }
  onProgress?.('shaders', all.length, all.length);

  let loadTestMs: number | null = null;
  if (slowGpuInputSize !== null) {
    // Cold clocks only add time and warm up with time worked, not run count.
    const { size } = runners.segmentation;
    const blank = new ImageData(size, size);
    loadTestMs = Infinity;
    const start = performance.now();
    do {
      const t = performance.now();
      await runners.segmentation.run(blank);
      loadTestMs = Math.min(loadTestMs, performance.now() - t);
    } while (
      loadTestMs > SLOW_GPU_MS &&
      performance.now() - start < SLOW_GPU_TEST_BUDGET_MS
    );
    if (loadTestMs > SLOW_GPU_MS) {
      runners.segmentation = segmentationAt(slowGpuInputSize);
      await runners.segmentation.warmUp();
    }
  }
  return { runners, loadTestMs };
}
