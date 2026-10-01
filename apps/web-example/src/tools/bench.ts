import {
  httpSearchClient,
  WebCardScanner,
  type KernelProfile,
  type ProfiledModel,
  type ScannedCard,
  type ScanTimings,
} from '@cardnexus/web-card-scanner';
import { reportPageErrors } from '../lib/pageErrors.ts';
import { errorMessage, fetchBackendHealth } from '../scanner.ts';

const logEl = document.getElementById('log')!;
const resultsEl = document.getElementById('results')!;
const paramsEl = document.getElementById('params')!;
const runBtn = document.getElementById('run') as HTMLButtonElement;
const saveBtn = document.getElementById('save') as HTMLButtonElement;
const copyBtn = document.getElementById('copy') as HTMLButtonElement;

const params = new URLSearchParams(location.search);
const int = (name: string, fallback: number) => {
  const raw = Number(params.get(name) ?? NaN);
  return Number.isFinite(raw) && raw >= 0 ? Math.floor(raw) : fallback;
};
const WARMUP = int('warmup', 2);
const RUNS = Math.max(1, int('runs', 10));
const LIMIT = int('limit', 0);
const GAMES = (params.get('games') ?? '')
  .split(',')
  .map((g) => g.trim())
  .filter(Boolean);
const INPUT = int('input', 0) || undefined;

const PROFILED: ProfiledModel[] = [
  'segmentation',
  'cardRecognition',
  'setSymbolDetection',
  'setSymbolRecognition',
  'colorBar',
];

const STAGES = [
  'captureMs',
  'segMs',
  'decodeMs',
  'embedMs',
  'searchMs',
  'stagesMs',
  'otherMs',
  'totalMs',
] as const;
type Stage = (typeof STAGES)[number];

function stagesOf(t: ScanTimings): Record<Stage, number> {
  const named = {
    captureMs: t.captureMs,
    segMs: t.segMs,
    decodeMs: t.decodeMs,
    embedMs: t.embedMs,
    searchMs: t.searchMs,
    stagesMs: t.stagesMs,
  };
  const accounted = Object.values(named).reduce((s, v) => s + v, 0);
  return { ...named, otherMs: t.totalMs - accounted, totalMs: t.totalMs };
}

type Outcome =
  | 'correct'
  | 'belowThreshold'
  | 'wrongMatch'
  | 'noDetection'
  | 'gameMismatch';

interface BenchImage {
  game: string;
  cardIds: string[];
  file: string;
  url: string;
}

interface Manifest {
  source: string;
  engine: Record<string, string>;
  images: BenchImage[];
}

interface ScanRecord {
  outcome: Outcome;
  cards: number;
  stages: Record<Stage, number>;
}

/** A photo's last measured scan, to compare two runs photo by photo. */
interface PhotoRecord {
  file: string;
  game: string;
  outcome: Outcome;
  correctRuns: number;
  detectionConfidence: number | null;
  predictedGame: string | null;
  topScore: number | null;
  secondScore: number | null;
}

interface Measured {
  records: ScanRecord[];
  photos: PhotoRecord[];
  gpu: Record<string, KernelProfile | null>;
  elapsed: number;
}

interface Stats {
  mean: number;
  p50: number;
  p95: number;
  max: number;
}

/** The mobile harness's buckets, in its order. */
function classify(cards: readonly ScannedCard[], image: BenchImage): Outcome {
  const card = cards[0];
  if (!card) return 'noDetection';
  if (!card.candidateGames.includes(image.game)) return 'gameMismatch';
  const top = card.matches[0];
  if (!top) return 'belowThreshold';
  return image.cardIds.includes(top.cardId) ? 'correct' : 'wrongMatch';
}

function stats(values: number[]): Stats {
  if (values.length === 0) return { mean: 0, p50: 0, p95: 0, max: 0 };
  const sorted = [...values].sort((a, b) => a - b);
  const at = (p: number) => sorted[Math.ceil(p * sorted.length) - 1];
  return {
    mean: sorted.reduce((s, v) => s + v, 0) / sorted.length,
    p50: at(0.5),
    p95: at(0.95),
    max: sorted[sorted.length - 1],
  };
}

const round = (v: number) => Math.round(v * 1000) / 1000;

async function fetchJson<T>(url: string): Promise<T> {
  const res = await fetch(url);
  if (!res.ok) throw new Error(`${url}: ${res.status}`);
  return (await res.json()) as T;
}

/** Decoded once per photo, so the scan loop never measures JPEG decoding. */
async function loadImage(url: string): Promise<HTMLImageElement> {
  const img = new Image();
  img.src = url;
  await img.decode();
  return img;
}

const yieldToPaint = () => new Promise((r) => setTimeout(r, 0));

function table(head: string[], rows: string[][]): string {
  const cells = (tag: string, values: string[]) =>
    values.map((v) => `<${tag}>${v}</${tag}>`).join('');
  return `<table><tr>${cells('th', head)}</tr>${rows
    .map((r) => `<tr>${cells('td', r)}</tr>`)
    .join('')}</table>`;
}

async function measure(
  scanner: WebCardScanner,
  images: BenchImage[],
): Promise<Measured> {
  const records: ScanRecord[] = [];
  const photos: PhotoRecord[] = [];
  const started = performance.now();
  for (const [i, image] of images.entries()) {
    logEl.textContent = `scanning ${i + 1}/${images.length}  ${image.file}`;
    await yieldToPaint();
    const img = await loadImage(image.url);
    let last: readonly ScannedCard[] = [];
    let outcome: Outcome = 'noDetection';
    let correctRuns = 0;
    for (let r = 0; r < WARMUP + RUNS; r++) {
      const { cards, timings } = await scanner.scan(img);
      if (r < WARMUP) continue;
      outcome = classify(cards, image);
      if (outcome === 'correct') correctRuns++;
      last = cards;
      records.push({ outcome, cards: cards.length, stages: stagesOf(timings) });
    }
    const card = last[0];
    const score = (i: number) => {
      const m = card?.matches[i];
      return m ? round(m.score) : null;
    };
    photos.push({
      file: image.file,
      game: image.game,
      outcome,
      correctRuns,
      detectionConfidence: card ? round(card.detectionConfidence) : null,
      predictedGame: card?.predictedGame ?? null,
      topScore: score(0),
      secondScore: score(1),
    });
  }
  const elapsed = (performance.now() - started) / 1000;

  logEl.textContent = 'profiling the models…';
  await yieldToPaint();
  const gpu: Measured['gpu'] = {};
  for (const model of PROFILED) gpu[model] = await scanner.profile(model);
  return { records, photos, gpu, elapsed };
}

let summaryJson = '';

async function run() {
  runBtn.disabled = saveBtn.disabled = copyBtn.disabled = true;
  resultsEl.innerHTML = '';
  logEl.textContent = 'loading the suite…';

  const manifest = await fetchJson<Manifest>('/bench/manifest.json');
  let images = manifest.images;
  if (GAMES.length) images = images.filter((i) => GAMES.includes(i.game));
  if (LIMIT) images = images.slice(0, LIMIT);
  if (images.length === 0) throw new Error('the suite selected no photos');

  logEl.textContent = 'checking the backend…';
  await fetchBackendHealth().catch(() => {
    throw new Error(
      'backend unreachable — run `yarn workspace web-example server` first',
    );
  });

  const scanner = await WebCardScanner.create({
    modelsBaseUrl: '/models',
    search: httpSearchClient('/api'),
    // No state carried across photos, and every searchMs is a real search.
    scanMode: 'multiple',
    useSidewaysFlipCache: false,
    reuseSearchResults: false,
    // One size for the whole run: ?input= or the library's default.
    ...(INPUT && { segmentationInputSize: INPUT }),
    slowGpuInputSize: null,
    onProgress: (stage, done, total) => {
      logEl.textContent = `loading ${stage}: ${((100 * done) / total).toFixed(0)}%`;
    },
  });
  const { records, photos, gpu, elapsed } = await measure(
    scanner,
    images,
  ).finally(() => scanner.dispose());

  const full = records.filter((r) => r.cards > 0);
  const count = (o: Outcome) => records.filter((r) => r.outcome === o).length;
  const correct = count('correct');
  const perStage = Object.fromEntries(
    STAGES.map((s) => [s, stats(full.map((r) => r.stages[s]))]),
  ) as Record<Stage, Stats>;
  const games = new Set(images.map((i) => i.game)).size;

  const summary = {
    run: {
      date: new Date().toISOString().slice(0, 10),
      engine: manifest.engine,
      adapter: gpu.segmentation?.adapter ?? 'unknown',
      platform: navigator.userAgent,
      buildMode: import.meta.env.MODE,
      segmentationInputSize: scanner.segmentationInputSize,
      models: 'apps/web-example/public/models (f16 conv weights, HWC4)',
      images: `${images.length} photos, ${games} games, from ${manifest.source}`,
      conditions:
        'still photos, no camera; scanMode multiple; search-result reuse off; ' +
        "sideways-flip cache off; one decoded photo reused across a photo's " +
        'runs, captured by the scanner on every scan (in captureMs)',
      elapsedSeconds: round(elapsed),
    },
    methodology: {
      warmupPerPhoto: WARMUP,
      measuredPerPhoto: RUNS,
      basis:
        'mean ms per scan over the scans that ran the full pipeline ' +
        '(detections > 0); otherMs is totalMs minus the named stages',
    },
    summary: Object.fromEntries(
      STAGES.map((s) => [s, round(perStage[s].mean)]),
    ) as Record<Stage, number>,
    scans: {
      measured: records.length,
      fullPipeline: full.length,
      correct,
      accuracyPercent: records.length
        ? round((100 * correct) / records.length)
        : 0,
      noDetection: count('noDetection'),
      gameMismatch: count('gameMismatch'),
      belowThreshold: count('belowThreshold'),
      wrongMatch: count('wrongMatch'),
    },
    photos,
  };
  summaryJson = `${JSON.stringify(summary, null, 2)}\n`;

  const ms = (v: number) => v.toFixed(2);
  const label = (key: string) =>
    key.replace(/Percent$/, '').replace(/[A-Z]/g, (c) => ` ${c.toLowerCase()}`);
  resultsEl.innerHTML = [
    '<h4>Stages, ms per scan</h4>',
    table(
      ['stage', 'mean', 'p50', 'p95', 'max'],
      STAGES.map((s) => [
        s.replace(/Ms$/, ''),
        ms(perStage[s].mean),
        ms(perStage[s].p50),
        ms(perStage[s].p95),
        ms(perStage[s].max),
      ]),
    ),
    '<h4>Scans</h4>',
    table(Object.keys(summary.scans).map(label), [
      Object.entries(summary.scans).map(([key, v]) =>
        key === 'accuracyPercent' ? `${v.toFixed(1)}%` : String(v),
      ),
    ]),
    `<h4>GPU time per model <span class="dim">${summary.run.adapter}</span></h4>`,
    table(
      ['model', 'GPU ms', 'dispatches', 'slowest kernel'],
      PROFILED.map((m) => {
        const p = gpu[m];
        const top = p?.kernels[0];
        return [
          m,
          p ? ms(p.totalMs) : '—',
          p ? String(p.dispatches) : '—',
          top ? `${top.name} ${ms(top.ms)}` : '—',
        ];
      }),
    ),
  ].join('');

  logEl.textContent =
    `done in ${elapsed.toFixed(0)} s — ${records.length} measured scans over ` +
    `${images.length} photos, ${ms(perStage.totalMs.mean)} ms per scan, ` +
    `segmentation at ${scanner.segmentationInputSize}`;
  runBtn.disabled = saveBtn.disabled = copyBtn.disabled = false;
}

paramsEl.textContent = [
  `${WARMUP} warm-up + ${RUNS} measured per photo`,
  `input ${INPUT ?? 'default'}`,
  LIMIT && `limit ${LIMIT}`,
  GAMES.length && `games ${GAMES.join(',')}`,
]
  .filter(Boolean)
  .join(' · ');
reportPageErrors((message) => {
  logEl.textContent += `\n${message}`;
});

runBtn.addEventListener('click', () => {
  void run().catch((e: unknown) => {
    logEl.textContent = `error: ${errorMessage(e)}`;
    runBtn.disabled = false;
  });
});

saveBtn.addEventListener('click', () => {
  const blob = new Blob([summaryJson], { type: 'application/json' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = `benchmark-web-${new Date().toISOString().slice(0, 10)}.json`;
  a.click();
  setTimeout(() => URL.revokeObjectURL(url), 0);
});

copyBtn.addEventListener('click', () => {
  void navigator.clipboard.writeText(summaryJson).then(() => {
    logEl.textContent += '\ncopied the summary JSON to the clipboard';
  });
});
