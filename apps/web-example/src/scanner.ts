/** The one scanner instance of the page. The scanner owns a GPU device and
 *  ~20 MB of weights, and React StrictMode runs effects twice in
 *  development, so creation is memoized here rather than in a hook. */

import { httpSearchClient, WebCardScanner } from '@cardnexus/web-card-scanner';

export type Progress = (stage: string, done: number, total: number) => void;
export type Report = (message: string) => void;

let pending: Promise<WebCardScanner> | undefined;
const listeners = new Set<Progress>();
const errorListeners = new Set<Report>();

export function loadScanner(
  onProgress?: Progress,
  onGpuError?: Report,
): Promise<WebCardScanner> {
  if (onProgress) listeners.add(onProgress);
  if (onGpuError) errorListeners.add(onGpuError);
  pending ??= WebCardScanner.create({
    modelsBaseUrl: '/models',
    search: httpSearchClient('/api'),
    onProgress: (...args) => listeners.forEach((l) => l(...args)),
    onGpuError: (message) => errorListeners.forEach((l) => l(message)),
  });
  return pending;
}

export async function fetchBackendHealth(): Promise<{
  games: Record<string, number>;
}> {
  const res = await fetch('/api/health');
  if (!res.ok) throw new Error(`health ${res.status}`);
  return res.json();
}

export const errorMessage = (e: unknown) =>
  e instanceof Error ? e.message : String(e);
