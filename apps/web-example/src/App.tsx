import { useEffect, useState } from 'react';
import type { ScanMode } from '@cardnexus/web-card-scanner';
import { Results } from './components/Results.tsx';
import { Stats } from './components/Stats.tsx';
import { Viewport } from './components/Viewport.tsx';
import { useCardConfirmation } from './hooks/useCardConfirmation.ts';
import { useScanLoop } from './hooks/useScanLoop.ts';
import { useScanner } from './hooks/useScanner.ts';

/** The boot sequence reports plain strings; three words tell the states apart. */
function toneOf(status: string, ready: boolean): 'ok' | 'busy' | 'error' {
  if (/error|unreachable|not available/i.test(status)) return 'error';
  return ready ? 'ok' : 'busy';
}

export function App() {
  const { scanner, status, setStatus } = useScanner();
  const loop = useScanLoop(scanner, setStatus);
  const ready = scanner !== null;
  const confirmation = useCardConfirmation();
  const [mode, setMode] = useState<ScanMode>('single');

  useEffect(() => {
    if (scanner) scanner.scanMode = mode;
  }, [scanner, mode]);

  const { confirm } = confirmation;
  useEffect(() => {
    if (loop.webcam) confirm(loop.cards);
  }, [loop.cards, loop.webcam, confirm]);

  // ?img=<url>: scan a fixed image on load (demos, automated checks).
  const { loadImage } = loop;
  useEffect(() => {
    const url = new URLSearchParams(location.search).get('img');
    if (ready && url) void loadImage(url);
  }, [ready, loadImage]);

  return (
    <div className="app">
      <header className="topbar">
        <div className="brand">
          <span className="brand-mark" aria-hidden="true" />
          <div>
            <h1>Card Scanner</h1>
            <p>TIE (WebGPU) · React example</p>
          </div>
        </div>
        <div className={`status status-${toneOf(status, ready)}`}>
          <span className="status-dot" aria-hidden="true" />
          <span className="status-text">{status}</span>
        </div>
      </header>

      <main className="layout">
        <section className="panel">
          <div className="toolbar">
            <button
              className={loop.webcam ? 'btn btn-danger' : 'btn btn-primary'}
              disabled={!ready}
              onClick={
                loop.webcam ? loop.stopWebcam : () => void loop.startWebcam()
              }
            >
              {loop.webcam ? 'Stop webcam' : 'Start webcam'}
            </button>
            <label className={`btn${ready ? '' : ' is-disabled'}`}>
              Open image
              <input
                type="file"
                accept="image/*"
                disabled={!ready}
                onChange={(e) => {
                  const file = e.target.files?.[0];
                  if (file) void loop.loadImage(URL.createObjectURL(file));
                  e.target.value = '';
                }}
              />
            </label>
            <div className="segmented" role="group" aria-label="Scan mode">
              <button
                className={mode === 'single' ? 'is-active' : ''}
                onClick={() => setMode('single')}
              >
                Single card
              </button>
              <button
                className={mode === 'multiple' ? 'is-active' : ''}
                onClick={() => setMode('multiple')}
              >
                Multiple
              </button>
            </div>
            <span className="toolbar-gap" />
            <button
              className="btn"
              disabled={!ready || !loop.source}
              onClick={() => void loop.scanOnce()}
            >
              Scan once
            </button>
            <button
              className={`btn${loop.looping ? ' is-active' : ''}`}
              disabled={!ready || !loop.source}
              onClick={loop.toggleLoop}
            >
              {loop.looping ? 'Pause loop' : 'Loop'}
            </button>
          </div>
          <div className="stage">
            <Viewport source={loop.source} cards={loop.cards} />
            {!loop.source && (
              <div className="stage-empty">
                <strong>Nothing to scan yet</strong>
                <span>
                  {ready
                    ? 'Start the webcam or open an image.'
                    : 'Models are loading…'}
                </span>
              </div>
            )}
          </div>
        </section>

        <aside className="sidebar">
          <Stats
            rate={loop.rate}
            timings={loop.timings}
            stall={loop.stall}
            cardCount={loop.cards.length}
            scanner={scanner}
          />
          <Results
            live={loop.cards}
            progress={confirmation.progress}
            history={confirmation.history}
            onRemove={confirmation.remove}
            onClear={confirmation.clear}
          />
        </aside>
      </main>
    </div>
  );
}
