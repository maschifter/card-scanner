/**
 * Percentage for display. Several DetectedCard fields are optional — a card
 * that was detected but not identified has no confidenceScore — so formatting
 * them directly yields "NaN%".
 */
export function pct(value?: number, digits = 1): string {
  return Number.isFinite(value) ? `${(value! * 100).toFixed(digits)}%` : '—';
}

/** Native code returns bare paths; `Image` needs a `file://` URI. */
export function fileUri(path: string): string {
  return path.startsWith('file://') ? path : `file://${path}`;
}
