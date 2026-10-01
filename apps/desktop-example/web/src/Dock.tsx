import { useLastMeasured, useScannerState } from './useScannerState';
import { emptyScan, type ScannedCard } from './types';
import './dock.css';

/**
 * The OBS dock: the controls the overlay does not have. Shares the overlay's
 * bundle, selected by ?view=dock, so there is still one file to serve.
 */
export default function Dock({ port }: { port: number }) {
  const { state, connection, send } = useScannerState(port);
  const scan = state.scan ?? emptyScan;
  const active = state.emitted ?? state.candidate;

  const perf = useLastMeasured(scan);

  return (
    <div className="dock">
      <div className="row">
        <span className={`dot ${connection}`} />
        <strong>{connection === 'open' ? state.status : connection}</strong>
        <span className="grow" />
        <button
          className={state.mode === 'auto' ? 'on' : ''}
          onClick={() => send({ command: 'set_mode', mode: 'auto' })}
        >
          Auto
        </button>
        <button
          className={state.mode === 'manual' ? 'on' : ''}
          onClick={() => send({ command: 'set_mode', mode: 'manual' })}
        >
          Manual
        </button>
      </div>

      <div className="panel">
        {active ? (
          <>
            {active.imageUrl ? <img className="art" src={active.imageUrl} alt="" /> : null}
            <div className="grow">
              <div className="name">{active.name || active.cardId}</div>
              <div className="meta">
                {[active.set, active.game].filter(Boolean).join(' · ')}
              </div>
              <div className="meta">match {active.score.toFixed(3)}</div>
            </div>
          </>
        ) : (
          <div className="meta">
            {scan.detections === 0
              ? 'no card in the scan region'
              : `${scan.detections} detected · ${scan.game || '?'}` +
                (scan.topScore > 0 ? ` · best ${scan.topScore.toFixed(3)}` : ' · no match')}
          </div>
        )}
      </div>

      <div className="row">
        {/* Always visible so it does not appear and vanish with the mode. */}
        <button
          disabled={!state.candidate || state.mode !== 'manual'}
          onClick={() => send({ command: 'emit_current' })}
        >
          Emit
        </button>
        <button disabled={!state.emitted} onClick={() => send({ command: 'clear_emitted' })}>
          Clear
        </button>
      </div>

      <label className="row slider">
        <span>Accept</span>
        <input
          type="range"
          min={0.3}
          max={0.95}
          step={0.01}
          value={state.settings?.acceptScore ?? 0.6}
          onChange={(e) =>
            send({ command: 'set_settings', acceptScore: Number(e.target.value) })
          }
        />
        <span className="num">{(state.settings?.acceptScore ?? 0.6).toFixed(2)}</span>
      </label>

      {perf ? (
        <div className="row perf">
          <span>
            yolo {perf.yoloMs.toFixed(1)} · prep {perf.preprocMs.toFixed(1)} ·
            embed {perf.embedMs.toFixed(1)} · db {perf.dbSearchMs.toFixed(1)} ·
            total {perf.ms.toFixed(1)} ms
          </span>
        </div>
      ) : null}

      <div className="history">
        {state.history?.map((card: ScannedCard) => (
          <button
            key={card.cardId}
            className="entry"
            title="Put this back on stream"
            onClick={() => send({ command: 'emit_from_history', cardId: card.cardId })}
          >
            {card.imageUrl ? <img src={card.imageUrl} alt="" /> : <span className="blank" />}
            <span className="entry-name">{card.name || card.cardId}</span>
          </button>
        ))}
      </div>
    </div>
  );
}
