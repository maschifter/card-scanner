import type { BenchmarkRecord } from '@cardnexus/card-scanner';

/**
 * Aggregations over a benchmark run, for display on BenchmarkScreen.
 * JSON provided by native side.
 *
 * Rules for aggregation:
 * - scan only contributes to a stage average if full pipeline ran for it (`detectionCount > 0`).
 * - if YOLO detects nothing scan does not contribute to any average.
 */

const ranFullPipeline = (r: BenchmarkRecord) => r.detectionCount > 0;
const predictedGames = (r: BenchmarkRecord) =>
  r.yoloPredictedGames.split(' ').filter(Boolean);

/**
 * The expected game was not among YOLO's predictions, so its database was
 * never searched and the right card could not have been found.
 */
const gameMismatched = (r: BenchmarkRecord) =>
  !predictedGames(r).includes(r.game);

const mean = (values: number[]) =>
  values.length === 0
    ? 0
    : values.reduce((sum, v) => sum + v, 0) / values.length;

const max = (values: number[]) =>
  values.length === 0 ? 0 : Math.max(...values);

export const measured = (records: BenchmarkRecord[]) =>
  records.filter((r) => !r.isWarmup);

/**
 * A photo with no expected id was matched against nothing, so it belongs in no
 * accuracy bucket. It still counts towards the timings.
 */
const scorable = (r: BenchmarkRecord) => r.outcome !== 'no_ground_truth';

/** Core pipeline stages */
const STAGE_FIELDS: [label: string, key: keyof BenchmarkRecord][] = [
  ['yolo', 'yoloMs'],
  ['save', 'saveMs'],
  ['embed', 'embedMs'],
  ['dbSearch', 'dbSearchMs'],
  ['preproc', 'preprocMs'],
];

/** Per-step fields of the optional, game-specific models. */
const SET_SYMBOL_FIELDS: [label: string, key: keyof BenchmarkRecord][] = [
  ['yolo', 'setSymbolYoloMs'],
  ['preproc', 'setSymbolPreprocMs'],
  ['embed', 'setSymbolEmbedMs'],
  ['dbSearch', 'setSymbolDbSearchMs'],
];

const FAB_COLOR_FIELDS: [label: string, key: keyof BenchmarkRecord][] = [
  ['classify', 'fabColorClassifyMs'],
];

export interface StageStat {
  label: string;
  mean: number;
  max: number;
  scans: number; // Scans this stage actually ran on — the mean's denominator
}

export interface ExtraModelStat {
  label: string;
  ran: number;
  outOf: number; // How many scans ran this model, out of the pipeline scans
  stages: StageStat[];
}

export interface GameStat {
  game: string;
  scans: number; // How many scans of this game ran the full pipeline
  totalScans: number; // Scans of this game that could be scored
  correct: number;
  meanMs: number;
}

export interface FailureCounts {
  noDetection: number;
  gameMismatch: number;
  belowThreshold: number;
  wrongMatch: number;
}

export interface BenchmarkSummary {
  totalScans: number; // Measured scans that had an expected id to check against
  correct: number;
  accuracy: number;
  totalCards: number;

  /**
   * Failure modes, mutually exclusive (a scan is counted in exactly one of these buckets).
   * The sum of these four counts is always equal to totalScans - correct.
   */
  noDetection: number;
  gameMismatch: number;
  belowThreshold: number;
  wrongMatch: number;
  cards: FailureCounts & { correct: number };

  /** Scans behind every timing below. */
  pipelineScans: number;
  meanMs: number;

  stages: StageStat[];
  meanMsWithColdStart: number;
  coldStartScans: number;

  extraModels: ExtraModelStat[];
  games: GameStat[];
}

/**
 * Treating 0ms output as a failure to measure, so the mean is not skewed by them.
 */
const statsFor = (
  records: BenchmarkRecord[],
  fields: [string, keyof BenchmarkRecord][],
): StageStat[] =>
  fields.map(([label, key]) => {
    const values = records
      .map((r) => r[key] as number)
      .filter((value) => value > 0);
    return {
      label,
      mean: mean(values),
      max: max(values),
      scans: values.length,
    };
  });

const extraModelStat = (
  label: string,
  records: BenchmarkRecord[],
  ranKey: keyof BenchmarkRecord,
  fields: [string, keyof BenchmarkRecord][],
): ExtraModelStat | null => {
  const ran = records.filter((r) => r[ranKey] as boolean);
  if (ran.length === 0) {
    return null;
  }
  return {
    label,
    ran: ran.length,
    outOf: records.length,
    stages: statsFor(ran, fields),
  };
};

type Classification = keyof FailureCounts | 'correct';

/**
 * Mutually exclusive classification of a scan, for counting correct vs. failure modes.
 */
const classify = (r: BenchmarkRecord): Classification => {
  if (!ranFullPipeline(r)) {
    return 'noDetection';
  }
  if (gameMismatched(r)) {
    return 'gameMismatch';
  }
  if (r.outcome === 'below_threshold') {
    return 'belowThreshold';
  }
  if (r.outcome === 'correct') {
    return 'correct';
  }
  return 'wrongMatch';
};

const gameStats = (
  measuredRecords: BenchmarkRecord[],
  pipelineRecords: BenchmarkRecord[],
): GameStat[] => {
  const order: string[] = [];
  const byGame = new Map<string, BenchmarkRecord[]>();
  for (const r of measuredRecords) {
    const list = byGame.get(r.game);
    if (list) {
      list.push(r);
    } else {
      order.push(r.game);
      byGame.set(r.game, [r]);
    }
  }

  return order.map((game) => {
    const scored = (byGame.get(game) ?? []).filter(scorable);
    const ran = pipelineRecords.filter((r) => r.game === game);
    return {
      game,
      scans: ran.length,
      totalScans: scored.length,
      correct: scored.filter((r) => classify(r) === 'correct').length,
      meanMs: mean(ran.map((r) => r.totalMs)),
    };
  });
};

const emptyCounts = (): FailureCounts & { correct: number } => ({
  correct: 0,
  noDetection: 0,
  gameMismatch: 0,
  belowThreshold: 0,
  wrongMatch: 0,
});

/**
 * Collapse scans to the distinct cards behind them. Every iteration of one
 * image classifies the same way, so the card is counted by its first scan.
 */
const countByCard = (runs: BenchmarkRecord[]) => {
  const seen = new Map<string, Classification>();
  for (const r of runs) {
    const key = `${r.game}|${r.cardId}`;
    if (!seen.has(key)) {
      seen.set(key, classify(r));
    }
  }
  const counts = emptyCounts();
  for (const c of seen.values()) {
    counts[c] += 1;
  }
  return { counts, total: seen.size };
};

/**
 * Build every figure the result card shows.
 * @param records - Every record from the run, warmups included
 */
export const summarize = (records: BenchmarkRecord[]): BenchmarkSummary => {
  const runs = measured(records);
  const scored = runs.filter(scorable);
  const pipeline = runs.filter(ranFullPipeline);

  const scanCounts = emptyCounts();
  for (const s of scored) {
    scanCounts[classify(s)] += 1;
  }
  const byCard = countByCard(scored);
  const withColdStart = records.filter(ranFullPipeline);

  return {
    totalScans: scored.length,
    correct: scanCounts.correct,
    accuracy:
      scored.length === 0 ? 0 : (100 * scanCounts.correct) / scored.length,
    totalCards: byCard.total,

    noDetection: scanCounts.noDetection,
    gameMismatch: scanCounts.gameMismatch,
    belowThreshold: scanCounts.belowThreshold,
    wrongMatch: scanCounts.wrongMatch,
    cards: byCard.counts,

    pipelineScans: pipeline.length,
    meanMs: mean(pipeline.map((r) => r.totalMs)),

    stages: statsFor(pipeline, STAGE_FIELDS),
    meanMsWithColdStart: mean(withColdStart.map((r) => r.totalMs)),
    coldStartScans: withColdStart.length,

    extraModels: [
      extraModelStat(
        'MTG set symbol',
        pipeline,
        'setSymbolRan',
        SET_SYMBOL_FIELDS,
      ),
      extraModelStat('FAB color', pipeline, 'fabColorRan', FAB_COLOR_FIELDS),
    ].filter((m): m is ExtraModelStat => m !== null),

    games: gameStats(runs, pipeline),
  };
};
