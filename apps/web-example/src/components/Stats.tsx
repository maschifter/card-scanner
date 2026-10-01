import { useState } from 'react';
import type {
  KernelProfile,
  ScanTimings,
  WebCardScanner,
} from '@cardnexus/web-card-scanner';

const ms = (v: number) => `${v.toFixed(0)} ms`;

function Chip({
  label,
  value,
  tone,
}: {
  label: string;
  value: string;
  tone?: 'warn';
}) {
  return (
    <div className={`chip${tone ? ` chip-${tone}` : ''}`}>
      <span className="chip-value">{value}</span>
      <span className="chip-label">{label}</span>
    </div>
  );
}

export function Stats({
  rate,
  timings,
  stall,
  cardCount,
  scanner,
}: {
  rate: number;
  timings: ScanTimings | null;
  stall: number;
  cardCount: number;
  scanner: WebCardScanner | null;
}) {
  const [profile, setProfile] = useState<KernelProfile | null | 'busy'>();
  const stages: [string, number][] = timings
    ? [
        ['capture', timings.captureMs],
        ['segment', timings.segMs],
        ['decode', timings.decodeMs],
        ['embed', timings.embedMs],
        ['search', timings.searchMs],
        ['stages', timings.stagesMs],
      ]
    : [];
  return (
    <section className="panel stats">
      <h2>Performance</h2>
      <div className="chips">
        <Chip label="scans / s" value={rate ? rate.toFixed(1) : '—'} />
        <Chip label="per frame" value={timings ? ms(timings.totalMs) : '—'} />
        <Chip label="cards" value={timings ? String(cardCount) : '—'} />
        <Chip
          label="main-thread stall"
          value={timings ? ms(stall) : '—'}
          tone={stall > 50 ? 'warn' : undefined}
        />
      </div>
      {scanner && timings && (
        <div className="dim">
          segmentation input {scanner.segmentationInputSize}
          {scanner.loadTestMs !== null &&
            ` (load test ${scanner.loadTestMs.toFixed(0)} ms)`}
        </div>
      )}
      {scanner && (
        <div className="row-right">
          <button
            className="btn btn-link"
            disabled={profile === 'busy' || !timings}
            onClick={() => {
              setProfile('busy');
              void scanner.profile().then(setProfile);
            }}
          >
            {profile === 'busy' ? 'profiling…' : 'Profile GPU'}
          </button>
        </div>
      )}
      {profile === null && (
        <p className="empty">This device has no timestamp-query support.</p>
      )}
      {profile && profile !== 'busy' && (
        <div className="profile">
          <div className="dim">{profile.adapter}</div>
          <div className="dim">
            segmentation: {profile.totalMs.toFixed(1)} ms GPU over{' '}
            {profile.dispatches} dispatches
          </div>
          <dl className="stages stages-wide">
            {profile.kernels.slice(0, 12).map((k) => (
              <div key={k.name}>
                <dt>
                  {k.name} <span className="dim">×{k.dispatches}</span>
                </dt>
                <dd>{k.ms.toFixed(1)}</dd>
              </div>
            ))}
          </dl>
        </div>
      )}
      {timings && (
        <dl className="stages">
          {stages.map(([name, v]) => (
            <div key={name}>
              <dt>{name}</dt>
              <dd>{v.toFixed(0)}</dd>
            </div>
          ))}
        </dl>
      )}
    </section>
  );
}
