/**
 * Percentage for display. Several DetectedCard fields are optional — a card
 * that was detected but not identified has no confidenceScore — so formatting
 * them directly yields "NaN%".
 */
export function pct(value?: number, digits = 1): string {
  return Number.isFinite(value) ? `${(value! * 100).toFixed(digits)}%` : '—';
}
