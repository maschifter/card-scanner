/** `setStatus` lets the scan loop report its errors in the same status line. */

import { useEffect, useState } from 'react';
import type { WebCardScanner } from '@cardnexus/web-card-scanner';
import { reportPageErrors } from '../lib/pageErrors.ts';
import { errorMessage, fetchBackendHealth, loadScanner } from '../scanner.ts';

export function useScanner() {
  const [scanner, setScanner] = useState<WebCardScanner | null>(null);
  const [status, setStatus] = useState('starting…');
  const [progress, setProgress] = useState<{ label: string; value: number }>();

  useEffect(() => {
    let cancelled = false;
    const report = (msg: string) => {
      if (!cancelled) setStatus(msg);
    };
    const stopReporting = reportPageErrors(report);

    (async () => {
      if (!('gpu' in navigator)) {
        report('WebGPU is not available in this browser.');
        return;
      }
      report('checking backend…');
      let health;
      try {
        health = await fetchBackendHealth();
      } catch {
        report(
          'backend unreachable — run `yarn workspace web-example server` first.',
        );
        return;
      }
      const games = Object.keys(health.games).length;
      const cards = Object.values(health.games).reduce((a, b) => a + b, 0);
      report('loading models…');
      const s = await loadScanner(
        (stage, done, total) =>
          setProgress({
            label: `${stage === 'models' ? 'Loading' : 'Compiling'} models ${done}/${total}`,
            value: (stage === 'models' ? 0 : 0.5) + done / total / 2,
          }),
        (message) => setStatus(`WebGPU error: ${message}`),
      );
      if (cancelled) return;
      setScanner(s);
      report(`ready · ${games} games · ${cards.toLocaleString('en-US')} cards`);
    })().catch((e) => report(`error: ${errorMessage(e)}`));

    return () => {
      cancelled = true;
      stopReporting();
    };
  }, []);

  return { scanner, status, setStatus, progress };
}
