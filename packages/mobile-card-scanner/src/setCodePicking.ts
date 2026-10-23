/**
 * Picks a card's printing from a printed set-code read by scoring each
 * candidate code against the read's per-step probabilities (CTC forward
 * algorithm), instead of trusting the greedy text. A half-legible code
 * (`CHJM-EN060` for `CHIM-EN060`) still ranks the right printing first.
 *
 * Pure TypeScript with no native calls: the app supplies the candidates, for
 * example the print numbers of the matched card's printings.
 */

/** The fields of a set-code read (`DetectedCard.printedSetCode`) the scoring
 *  uses. */
export interface SetCodeDistribution {
  /** Row-major [steps x classes] probabilities. */
  probs: ArrayLike<number>;
  steps: number;
  classes: number;
  /** What each class reads as: the CTC blank first, then one character
   *  each. */
  classNames: readonly string[];
}

/** Language infixes a card prints that the catalog drops: BROL-062 is
 *  printed BROL-EN062, BROL-FR062, ...; the oldest sets use one letter. */
const LANGUAGE_INFIXES = [
  '',
  'EN',
  'FR',
  'DE',
  'IT',
  'SP',
  'PT',
  'JP',
  'KR',
  'AE',
  'TC',
  'SC',
  'E',
  'F',
  'G',
  'I',
  'S',
  'P',
];

/** Set prefix and card number of a catalog print number, uppercase: YGLD-A22,
 *  or IOC-25TH-102 for a reprint edition. Null without a dash. */
function splitPrintNumber(
  printNumber: string,
): { prefix: string; number: string } | null {
  const parts = printNumber.toUpperCase().split('-');
  return parts.length < 2
    ? null
    : { prefix: parts[0], number: parts[parts.length - 1] };
}

/** Folds characters a reader confuses and set prefixes use both ways (CP02,
 *  BODE): O to 0, I to 1, Z to 2. */
const foldLookAlikes = (code: string) =>
  code.replace(/O/g, '0').replace(/I/g, '1').replace(/Z/g, '2');

/** Compares print numbers by set prefix and card number, with look-alikes
 *  folded. Null without a dash. */
export function catalogKey(printNumber: string): string | null {
  const parts = splitPrintNumber(printNumber);
  return parts && foldLookAlikes(`${parts.prefix}-${parts.number}`);
}

/** The codes a card with this catalog print number can print. */
export function printedForms(printNumber: string): string[] {
  const parts = splitPrintNumber(printNumber);
  return parts
    ? LANGUAGE_INFIXES.map((infix) => `${parts.prefix}-${infix}${parts.number}`)
    : [];
}

const logAdd = (a: number, b: number) =>
  a === -Infinity
    ? b
    : b === -Infinity
      ? a
      : Math.max(a, b) + Math.log1p(Math.exp(-Math.abs(a - b)));

/**
 * Log-probability that the reader saw `code`, summed over every CTC
 * alignment; -Infinity for an empty code, an empty read, or a code with a
 * character the reader lacks.
 */
export function setCodeLogProb(
  read: SetCodeDistribution,
  code: string,
): number {
  const { probs, steps, classes, classNames } = read;
  const ids = [...code].map((c) => classNames.indexOf(c));
  if (!ids.length || !steps || ids.some((id) => id <= 0)) return -Infinity;
  // The label with a blank before, between and after every character.
  const extended = [0];
  for (const id of ids) extended.push(id, 0);
  const logp = (t: number, k: number) =>
    Math.log(Math.max(probs[t * classes + extended[k]], 1e-9));

  let alpha = extended.map((_, k) => (k < 2 ? logp(0, k) : -Infinity));
  for (let t = 1; t < steps; t++) {
    alpha = extended.map((label, k) => {
      let v = alpha[k];
      if (k > 0) v = logAdd(v, alpha[k - 1]);
      if (k > 1 && label !== 0 && label !== extended[k - 2]) {
        v = logAdd(v, alpha[k - 2]);
      }
      return v + logp(t, k);
    });
  }
  const end = extended.length;
  return logAdd(alpha[end - 1], alpha[end - 2]);
}

/** One candidate in a PrintingPick's ranking. */
export interface PrintingCandidate {
  /** Catalog print number, e.g. BROL-062. */
  printNumber: string;
  /** The printed form that scored best, e.g. BROL-EN062. */
  printed: string;
  /** Share of probability among all candidates, 0-1. */
  share: number;
}

export interface PrintingPick {
  /** The chosen print number, or null when the read cannot settle it. */
  pick: string | null;
  /** Every candidate printing the same code as the pick (editions and
   *  reprints the code cannot tell apart). */
  sharedBy: string[];
  /** Lead in nats over the best candidate printing a different code;
   *  Infinity when every candidate prints the same code. */
  margin: number;
  /** The best candidate's log-probability minus the best single path's;
   *  0 when the read is exactly that code. */
  support: number;
  /** The candidates, most likely first. */
  ranking: PrintingCandidate[];
}

export interface PickPrintingOptions {
  /** Lead in nats the pick needs over every different code. Default 6. */
  minMargin?: number;
  /** How far below the read's best path the pick may score. Default -12. */
  minSupport?: number;
}

/**
 * Picks the printing among `candidates` (catalog print numbers) that the read
 * supports. Picks one only when it leads every candidate printing a
 * different code by `minMargin` nats and the read backs it to within
 * `minSupport`; otherwise `pick` is null, so look at the next frame. The
 * defaults made no wrong pick on the model's validation set.
 */
export function pickPrinting(
  read: SetCodeDistribution,
  candidates: readonly string[],
  { minMargin = 6, minSupport = -12 }: PickPrintingOptions = {},
): PrintingPick {
  const scored = candidates
    .map((printNumber) => {
      let best = { printed: '', score: -Infinity };
      for (const printed of printedForms(printNumber)) {
        const score = setCodeLogProb(read, printed);
        if (score > best.score) best = { printed, score };
      }
      return { printNumber, ...best };
    })
    .filter((c) => Number.isFinite(c.score))
    .sort((a, b) => b.score - a.score);
  if (!scored.length) {
    return {
      pick: null,
      sharedBy: [],
      margin: 0,
      support: -Infinity,
      ranking: [],
    };
  }

  const best = scored[0];
  const key = catalogKey(best.printNumber);
  // The best any single label path can do: the per-step maximum, summed.
  let bestPath = 0;
  for (let t = 0; t < read.steps; t++) {
    let max = 1e-9;
    for (let k = 0; k < read.classes; k++) {
      max = Math.max(max, read.probs[t * read.classes + k]);
    }
    bestPath += Math.log(max);
  }
  const support = best.score - bestPath;
  // Editions share a printed code (YGLD-A22 and its Unlimited twin), so the
  // margin is over the best candidate printing a different one.
  const rival = scored.find((c) => catalogKey(c.printNumber) !== key);
  const margin = rival ? best.score - rival.score : Infinity;
  const total = scored.reduce((s, c) => s + Math.exp(c.score - best.score), 0);
  return {
    pick:
      margin >= minMargin && support >= minSupport ? best.printNumber : null,
    sharedBy: scored
      .filter((c) => catalogKey(c.printNumber) === key)
      .map((c) => c.printNumber),
    margin,
    support,
    ranking: scored.map((c) => ({
      printNumber: c.printNumber,
      printed: c.printed,
      share: Math.exp(c.score - best.score) / total,
    })),
  };
}
