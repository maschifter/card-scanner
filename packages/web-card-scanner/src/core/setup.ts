import tgpu from 'typegpu';
import {
  bufferSource,
  createOpfsCache,
  fromSafetensors,
  initRunntime,
  type nn,
  parseSafetensorsHeader,
  type Value,
  Yolo26Model,
} from '../runntime.ts';
import type { TgpuRoot } from 'typegpu';
import {
  CARD_EMBED_SIZE,
  COLOR_BAR_SIZE,
  LANGUAGE_INPUT_HEIGHT,
  LANGUAGE_INPUT_WIDTH,
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
import { CardLanguageModel } from '../models/cardLanguage.ts';
import { Runner } from '../inference/runner.ts';

export interface Runners {
  segmentation: Runner;
  cardRecognition: Runner;
  setSymbolDetection?: Runner;
  setSymbolRecognition?: Runner;
  colorBar?: Runner;
  language?: Runner;
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
  initRunntime(root); // half() checks the default root for shader-f16
  root.device.addEventListener('uncapturederror', (e) => {
    const message = (e as GPUUncapturedErrorEvent).error.message;
    console.error('WebGPU:', message);
    onGpuError?.(message);
  });
  void root.device.lost.then((info) => {
    // dispose() destroys the device on purpose
    if (info.reason !== 'destroyed')
      onGpuError?.(`device lost (${info.reason}): ${info.message}`);
  });
  return root;
}

/** Weights are OPFS-cached when available (Safari's support is patchy —
 *  plain fetches otherwise). */
export async function createRunners(
  root: TgpuRoot,
  base: string,
  onProgress: Progress,
  segmentationInputSize: number,
  slowGpuInputSize: number | null,
  matchSetSymbol: boolean,
  classifyFabColor: boolean,
  classifyLanguage: boolean,
): Promise<{ runners: Runners; loadTestMs: number | null }> {
  const cache = await createOpfsCache('web-card-scanner').catch(
    () => undefined,
  );
  // One model set stays cached: a miss on the first file is a new set.
  let first = true;
  const load = async (model: nn.AnyModule, dir: string) => {
    const url = `${base}/${dir}/model.safetensors`;

    // Only a whole model may be cached: not an HTML fallback, not an LFS
    // pointer, not a file still being uploaded.
    const download = async () => {
      for (let attempt = 1; ; attempt++) {
        let res: Response;
        let body: ArrayBuffer;
        try {
          res = await fetch(url);
          body = await res.arrayBuffer();
        } catch (e) {
          if (attempt === 3) throw e;
          await new Promise((r) => setTimeout(r, 500 * attempt));
          continue;
        }
        if (!res.ok) throw new Error(`${url}: ${res.status}`);
        const bytes = new Uint8Array(body);
        const { tensors } = await parseSafetensorsHeader(bufferSource(bytes));
        const end = Math.max(...[...tensors.values()].map((t) => t.end));
        if (bytes.length < end) throw new Error(`${url}: truncated`);
        if (first) await cache?.clear();
        return bytes;
      }
    };
    const bytes = cache
      ? await cache.getOrCompute(url, download)
      : await download();
    first = false;
    const sd = await fromSafetensors(bufferSource(bytes));
    await model.loadStateDict(sd, { root });
  };
  const seg = new Yolo26Model('n', {
    numClasses: NUM_GAME_CLASSES,
    task: 'segment',
  });
  const symbolDet =
    matchSetSymbol && new Yolo26Model('n', { numClasses: 1, task: 'detect' });
  const cardRec = new CardRecognitionModel();
  const symbolRec = matchSetSymbol && new SetSymbolRecognitionModel();
  const colorBar = classifyFabColor && new ColorBarModel();
  const language = classifyLanguage && new CardLanguageModel();
  const models = Object.entries({
    CardSegmentationModel: seg,
    SetSymbolDetectionModel: symbolDet,
    CardRecognitionModel: cardRec,
    SetSymbolRecognitionModel: symbolRec,
    ColorBarModel: colorBar,
    CardLanguageModel: language,
  }).flatMap(([dir, model]) => (model ? [{ dir, model }] : []));
  for (const [i, { dir, model }] of models.entries()) {
    onProgress?.('models', i, models.length);
    await load(model, dir);
  }
  onProgress?.('models', models.length, models.length);

  const segmentationAt = (size: number) =>
    new Runner(root, [3, size, size], 'scale255', (input: Value) => {
      const { levels, proto } = seg.forward(input);
      return [...levels, proto];
    });
  const imagenetAt = (size: number, model: nn.Module) =>
    new Runner(root, [3, size, size], 'imagenet', (input: Value) => [
      model.forward(input),
    ]);
  const runners: Runners = {
    segmentation: segmentationAt(segmentationInputSize),
    cardRecognition: imagenetAt(CARD_EMBED_SIZE, cardRec),
    ...(symbolDet && {
      setSymbolDetection: new Runner(
        root,
        [3, YOLO_INPUT_SIZE, YOLO_INPUT_SIZE],
        'scale255',
        (input) => [...symbolDet.forward(input)],
      ),
    }),
    ...(symbolRec && {
      setSymbolRecognition: imagenetAt(SYMBOL_EMBED_SIZE, symbolRec),
    }),
    ...(colorBar && { colorBar: imagenetAt(COLOR_BAR_SIZE, colorBar) }),
    ...(language && {
      language: new Runner(
        root,
        [3, LANGUAGE_INPUT_HEIGHT, LANGUAGE_INPUT_WIDTH],
        'minusOneToOne',
        (input) => [language.forward(input)],
      ),
    }),
  };

  const all = Object.values(runners);
  for (const [i, runner] of all.entries()) {
    onProgress?.('shaders', i, all.length);
    await runner.warmUp();
  }

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
  onProgress?.('shaders', all.length, all.length);
  return { runners, loadTestMs };
}
