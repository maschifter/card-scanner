/** ScannerPipeline::processDetection. */

import { SIDEWAYS_FLIP_CACHE_TTL_MS } from '../constants.ts';
import type {
  CardMatch,
  GameClassEntry,
  ScannedCard,
  ScannerOptions,
  SearchClient,
} from '../types.ts';
import type { CardDetection } from '../utils/decode.ts';
import type { Quad } from '../utils/geometry.ts';
import type { Box } from '../utils/box.ts';
import { l2Normalize } from '../utils/vector.ts';
import { rotateQuad180, type GpuDewarp } from '../inference/gpuFrame.ts';
import type { Runners } from './setup.ts';
import { extractTopGames } from './searchStrategy.ts';
import { matchSetSymbol } from './setSymbolProcessor.ts';
import { classifyFabColor } from './fabColorProcessor.ts';

export interface Found {
  det: CardDetection;
  quad: Quad | null;
  /** The mask quad was landscape: its 180° orientation is ambiguous. */
  sideways: boolean;
  /** The quad, or the detection box as a quad when there is none. */
  outline: Quad;
  /** The detection box in frame pixels (native card.boundingBox). */
  box: Box;
}

export type StageAcc = Record<'embed' | 'search' | 'stages', number>;

export type CardProcessorSettings = Required<
  Pick<
    ScannerOptions,
    | 'minGameConfidence'
    | 'disambiguationThreshold'
    | 'setSymbolDetectionThreshold'
    | 'useSidewaysFlipCache'
  >
> & { search: SearchClient };

/** One recognition attempt on one orientation of the card. */
interface Recognition {
  dewarp: ImageData;
  embedding: Float32Array;
  matches: CardMatch[];
}

export class CardProcessor {
  private flipCache: { flipped: boolean; at: number } | null = null;

  constructor(
    private readonly runners: Runners,
    private readonly mapping: readonly GameClassEntry[],
    private readonly settings: CardProcessorSettings,
    private readonly dewarper: GpuDewarp,
  ) {}

  async process(
    { det, quad, sideways, outline, box }: Found,
    frame: GPUTexture,
    acc: StageAcc,
  ): Promise<ScannedCard> {
    const { search: client, ...opts } = this.settings;
    const timed = async <T>(key: keyof StageAcc, f: () => Promise<T> | T) => {
      const t = performance.now();
      const v = await f();
      acc[key] += performance.now() - t;
      return v;
    };

    const candidateGames = extractTopGames(
      this.mapping,
      det.classScores,
      opts.minGameConfidence,
    );

    const recognize = async (flipped: boolean): Promise<Recognition> => {
      this.dewarper.run(frame, flipped ? rotateQuad180(outline) : outline);
      const { dewarp, embedding } = await timed('embed', () =>
        this.embedCard(),
      );
      const matches = candidateGames.length
        ? await timed('search', () =>
            client.searchCards([...embedding], candidateGames),
          )
        : [];
      return { dewarp, embedding, matches };
    };

    // A sideways quad is 180° ambiguous. Try the orientation that resolved
    // last time first (cache, 3 s TTL), then the other one when recognition
    // misses, and remember whichever matched.
    let flipped = sideways && this.cachedFlip() === true;
    let result = await recognize(flipped);

    if (sideways && result.matches.length === 0) {
      const retry = await recognize(!flipped);
      if (retry.matches.length) {
        result = retry;
        flipped = !flipped;
      }
    }
    if (sideways && result.matches.length && opts.useSidewaysFlipCache) {
      this.flipCache = { flipped, at: performance.now() };
    }

    const { dewarp, embedding, matches } = result;
    const stageGame = this.disambiguationGame(matches);
    const setSymbol =
      stageGame === 'mtg'
        ? await timed('stages', () =>
            matchSetSymbol(
              dewarp,
              this.runners.setSymbolDetection,
              this.runners.setSymbolRecognition,
              client,
              opts.setSymbolDetectionThreshold,
            ),
          )
        : null;
    const fabColor =
      stageGame === 'fab'
        ? await timed('stages', () =>
            classifyFabColor(dewarp, this.runners.colorBar),
          )
        : null;

    return {
      box,
      detectionConfidence: det.score,
      quad,
      dewarp,
      embedding,
      candidateGames,
      matches,
      predictedGame: matches[0]?.gameName ?? candidateGames[0] ?? null,
      setSymbol,
      fabColor,
    };
  }

  /** The game whose disambiguation stage should run, when the top two
   *  matches nearly tie; null otherwise. */
  private disambiguationGame(matches: CardMatch[]): string | null {
    if (matches.length < 2) return null;
    const margin = matches[0].score - matches[1].score;
    if (margin > this.settings.disambiguationThreshold) return null;
    return matches[0].gameName;
  }

  private cachedFlip(): boolean | null {
    const cached = this.flipCache;
    if (!this.settings.useSidewaysFlipCache || !cached) return null;
    const fresh = performance.now() - cached.at <= SIDEWAYS_FLIP_CACHE_TTL_MS;
    return fresh ? cached.flipped : null;
  }

  private async embedCard(): Promise<{
    dewarp: ImageData;
    embedding: Float32Array;
  }> {
    // Squash 403×640 → 224².
    const { outputs, pixels } = await this.runners.cardRecognition.runAndRead(
      this.dewarper.texture,
    );
    return { dewarp: pixels, embedding: l2Normalize(outputs[0].data) };
  }
}
