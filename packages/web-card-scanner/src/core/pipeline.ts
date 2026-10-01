/** The ScannerPipeline of the web port. Database search is delegated to a
 *  SearchClient: the backend owns the databases, clients ship embeddings. */

import type { TgpuRoot } from 'typegpu';
import {
  DEFAULT_DISAMBIGUATION_MARGIN,
  DEFAULT_MIN_GAME_CONFIDENCE,
  DEFAULT_SEGMENTATION_THRESHOLD,
  DEFAULT_SYMBOL_DETECTION_THRESHOLD,
  MASK_BOX_PADDING_PX,
  NUM_GAME_CLASSES,
  SLOW_GPU_INPUT_SIZE,
  YOLO_INPUT_SIZE,
  YOLO_STRIDES,
} from '../constants.ts';
import type {
  FrameSource,
  ScannedCard,
  ScannerOptions,
  ScanMode,
  ScanResult,
} from '../types.ts';
import {
  decodeCards,
  dropGroupBoxes,
  protoPlane,
  rawLevels,
  type CardDetection,
} from '../utils/decode.ts';
import { maskToQuad, type Pt, type Quad } from '../utils/geometry.ts';
import { bounds } from '../utils/box.ts';
import {
  letterboxPlacement,
  type Letterbox,
} from '../inference/gpuPreprocess.ts';
import { GpuDewarp, GpuFrame } from '../inference/gpuFrame.ts';
import { StickyTracker } from './cardSelection.ts';
import { cachedSearchClient } from './cachedSearchClient.ts';
import { CardProcessor, type Found, type StageAcc } from './cardProcessor.ts';
import { profileRunner, type ProfiledModel } from './profile.ts';
import {
  createDevice,
  createRunners,
  fetchGameClassMapping,
  type Runners,
} from './setup.ts';

export class WebCardScanner {
  /** See ScannerOptions.scanMode; changing it takes effect next frame. */
  scanMode: ScanMode;
  private readonly sticky = new StickyTracker();

  private constructor(
    private readonly root: TgpuRoot,
    private readonly runners: Runners,
    private readonly cards: CardProcessor,
    private readonly segmentationThreshold: number,
    scanMode: ScanMode,
    private readonly frames: GpuFrame,
    /** The fastest load-test run that decided the input size, else null. */
    readonly loadTestMs: number | null,
  ) {
    this.scanMode = scanMode;
  }

  get segmentationInputSize(): number {
    return this.runners.segmentation.size;
  }

  static async create(options: ScannerOptions): Promise<WebCardScanner> {
    const opts = {
      segmentationThreshold: DEFAULT_SEGMENTATION_THRESHOLD,
      minGameConfidence: DEFAULT_MIN_GAME_CONFIDENCE,
      disambiguationThreshold: DEFAULT_DISAMBIGUATION_MARGIN,
      setSymbolDetectionThreshold: DEFAULT_SYMBOL_DETECTION_THRESHOLD,
      scanMode: 'multiple' as ScanMode,
      useSidewaysFlipCache: true,
      reuseSearchResults: true,
      segmentationInputSize: YOLO_INPUT_SIZE,
      slowGpuInputSize: SLOW_GPU_INPUT_SIZE,
      ...options,
    };
    checkInputSize('segmentationInputSize', opts.segmentationInputSize);
    if (opts.slowGpuInputSize !== null) {
      checkInputSize('slowGpuInputSize', opts.slowGpuInputSize);
    }
    const search = opts.reuseSearchResults
      ? cachedSearchClient(opts.search)
      : opts.search;

    const base = opts.modelsBaseUrl.replace(/\/$/, '');
    const mapping = await fetchGameClassMapping(base);
    const root = await createDevice(opts.onGpuError);
    const { runners, loadTestMs } = await createRunners(
      root,
      base,
      opts.onProgress,
      opts.segmentationInputSize,
      opts.slowGpuInputSize,
    );

    const cards = new CardProcessor(
      runners,
      mapping,
      { ...opts, search },
      new GpuDewarp(root.device),
    );
    return new WebCardScanner(
      root,
      runners,
      cards,
      opts.segmentationThreshold,
      opts.scanMode,
      new GpuFrame(root.device),
      loadTestMs,
    );
  }

  async scan(source: FrameSource): Promise<ScanResult> {
    const t0 = performance.now();
    const frame = this.frames.upload(source);
    if (!frame) throw new Error('scan: the source has no pixels yet');
    const tCapture = performance.now();

    const letterbox = letterboxPlacement(
      frame.width,
      frame.height,
      this.segmentationInputSize,
    );
    const outs = await this.runners.segmentation.run(
      frame,
      letterbox.placement,
    );
    const tSeg = performance.now();

    // Three stride heads, then the prototype masks.
    const proto = protoPlane(outs[3]);
    const detections = decodeCards(rawLevels(outs, YOLO_STRIDES), proto, {
      numClasses: NUM_GAME_CLASSES,
      confThreshold: this.segmentationThreshold,
      maskPadding: MASK_BOX_PADDING_PX * letterbox.scale,
    });
    const tDecode = performance.now();

    const protoStride = this.segmentationInputSize / proto.w;
    const found = dropGroupBoxes(detections).map((det) =>
      locate(det, letterbox, protoStride),
    );
    const selected =
      this.scanMode === 'single'
        ? this.sticky.select(found, frame.width, frame.height)
        : found;

    const acc: StageAcc = { embed: 0, search: 0, stages: 0 };
    const cards: ScannedCard[] = [];
    for (const f of selected) {
      cards.push(await this.cards.process(f, frame, acc));
    }
    const t1 = performance.now();

    return {
      cards,
      timings: {
        captureMs: tCapture - t0,
        segMs: tSeg - tCapture,
        decodeMs: tDecode - tSeg,
        embedMs: acc.embed,
        searchMs: acc.search,
        stagesMs: acc.stages,
        perCardMs: t1 - tDecode,
        totalMs: t1 - t0,
      },
    };
  }

  /** Null without 'timestamp-query' or before the model has run. */
  profile(model: ProfiledModel = 'segmentation') {
    return profileRunner(this.root, this.runners[model]);
  }

  /** Destroys the GPU device; the scanner is unusable afterwards. */
  dispose() {
    this.root.destroy();
  }
}

/** YOLO26 downsamples 32×: other sizes fail in the graph or misplace boxes. */
export function checkInputSize(name: string, size: number): void {
  if (!Number.isInteger(size) || size <= 0 || size % 32 !== 0) {
    throw new Error(`${name} must be a positive multiple of 32, got ${size}`);
  }
}

/** A detection with its outline in frame pixels: the mask quad when there
 *  is one, else the detection box. */
export function locate(
  det: CardDetection,
  letterbox: Letterbox,
  protoStride: number,
): Found {
  const oriented = maskToQuad(det.mask, det.maskW, det.maskH);

  const toFrame = (p: Pt) =>
    letterbox.toSource(
      (det.maskX + p.x) * protoStride,
      (det.maskY + p.y) * protoStride,
    );
  const quad = oriented ? (oriented.quad.map(toFrame) as Quad) : null;

  const boxQuad: Quad = [
    letterbox.toSource(det.x1, det.y1),
    letterbox.toSource(det.x2, det.y1),
    letterbox.toSource(det.x2, det.y2),
    letterbox.toSource(det.x1, det.y2),
  ];

  return {
    det,
    quad,
    sideways: oriented?.sideways ?? false,
    outline: quad ?? boxQuad,
    box: bounds(boxQuad),
  };
}
