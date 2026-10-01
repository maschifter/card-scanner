import { useEffect, useState } from 'react';
import { useLastMeasured, useScannerState } from './useScannerState';
import { emptyScan, type ScanDiagnostics, type ScannedCard } from './types';
import './styles.css';

function CardPanel({ card, pending }: { card: ScannedCard; pending: boolean }) {
  const meta = [card.set, card.game].filter(Boolean).join(' · ');
  return (
    <div className={pending ? 'panel pending' : 'panel'}>
      {card.imageUrl ? (
        <img className="art" src={card.imageUrl} alt="" />
      ) : null}
      <div>
        {/* The id is the fallback: an empty name would read as a scanner
            failure rather than a lookup one. */}
        <div className="name">{card.name || card.cardId}</div>
        {meta ? <div className="meta">{meta}</div> : null}
        <div className="score">
          match {card.score.toFixed(3)}
          {pending ? ` · confirming ${card.detections}` : null}
        </div>
      </div>
    </div>
  );
}

/**
 * Says what the scanner is doing when no card is on screen: not connected, no
 * frames, nothing detected, or detected but scoring too low to accept.
 */
function ScanReadout({
  scan,
  connection,
}: {
  scan: ScanDiagnostics;
  connection: string;
}) {
  if (connection !== 'open') return <>{connection}</>;
  if (!scan.live) return <>waiting for frames</>;
  if (scan.detections === 0) return <>no card in the scan region</>;

  const where = `${scan.detections} detected · ${scan.game || '?'} ${scan.gameConfidence.toFixed(2)}`;
  if (scan.topScore <= 0) {
    return <>{where} · no database match</>;
  }
  return (
    <>
      {where} · best {scan.topScore.toFixed(3)}
      {scan.topScore < 0.6 ? ' (below 0.60)' : ''}
    </>
  );
}

/** The port is parsed once in main.tsx, same as for the dock. */
export default function App({ port }: { port: number }) {
  // On by default while the thing is being set up; ?debug=0 hides it for a
  // real broadcast.
  const [debug] = useState(
    () => new URLSearchParams(window.location.search).get('debug') !== '0',
  );
  const { state, connection } = useScannerState(port);
  const scan = state.scan ?? emptyScan;
  const perf = useLastMeasured(scan);

  // A candidate shows as soon as it exists: waiting for `emitted` leaves the
  // overlay blank through the stability window. Emitted outranks candidate.
  const active = state.emitted ?? state.candidate;
  const pending = !state.emitted && Boolean(state.candidate);

  // Kept mounted through the exit transition, or it vanishes instantly and
  // reads as a glitch on stream.
  const [lingering, setLingering] = useState<ScannedCard | null>(null);
  useEffect(() => {
    if (active) {
      setLingering(active);
      return;
    }
    const id = window.setTimeout(() => setLingering(null), 300);
    return () => window.clearTimeout(id);
  }, [active]);

  return (
    <>
      <div className="status">
        <div>{connection === 'open' ? state.status : connection}</div>
        {/* Mirrored here as well as the panel: one corner answers both "is
            it working" and "what did it read". */}
        {active ? (
          <div className="corner-card">
            {active.name || active.cardId}
            <span className="corner-score"> {active.score.toFixed(3)}</span>
          </div>
        ) : null}
        {debug ? (
          <div className="readout">
            <ScanReadout scan={scan} connection={connection} />
          </div>
        ) : null}
        {perf ? (
          <div className="readout perf">
            yolo {perf.yoloMs.toFixed(1)} · prep {perf.preprocMs.toFixed(1)} ·
            embed {perf.embedMs.toFixed(1)} · db {perf.dbSearchMs.toFixed(1)} ·
            total {perf.ms.toFixed(1)} ms
          </div>
        ) : null}
      </div>
      <div className={active ? 'card visible' : 'card'}>
        {lingering ? <CardPanel card={lingering} pending={pending} /> : null}
      </div>
    </>
  );
}
