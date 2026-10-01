import { useEffect, useRef } from 'react';
import type {
  ConfirmedCard,
  ConfirmProgress,
} from '../hooks/useCardConfirmation.ts';
import type { CardView } from '../hooks/useScanLoop.ts';
import { cardHue } from '../lib/overlay.ts';

const pct = (v: number, digits = 0) => `${(v * 100).toFixed(digits)}%`;

function Thumb({
  image,
}: {
  image: CanvasImageSource & { width: number; height: number };
}) {
  const ref = useRef<HTMLCanvasElement>(null);
  useEffect(() => {
    ref.current!.getContext('2d')!.drawImage(image, 0, 0);
  }, [image]);
  return (
    <canvas
      className="thumb"
      ref={ref}
      width={image.width}
      height={image.height}
    />
  );
}

function Tags({ card }: { card: Pick<CardView, 'setSymbol' | 'fabColor'> }) {
  return (
    <>
      {card.setSymbol && (
        <span className="tag tag-accent">
          set {card.setSymbol.setCode} · {pct(card.setSymbol.score)}
        </span>
      )}
      {card.fabColor && (
        <span className={`tag tag-${card.fabColor.color}`}>
          pitch {card.fabColor.color} · {pct(card.fabColor.score)}
        </span>
      )}
    </>
  );
}

function LiveRow({
  card,
  index,
  progress,
}: {
  card: CardView;
  index: number;
  progress: ConfirmProgress | null;
}) {
  const [top, ...rest] = card.matches;
  const hue = cardHue(index);
  const confirming = progress && top && progress.cardId === top.cardId;
  return (
    <article className="result" style={{ '--hue': hue } as React.CSSProperties}>
      <Thumb image={card.thumb} />
      <div className="result-body">
        {top ? (
          <>
            <div className="result-title">{top.name ?? top.cardId}</div>
            <div className="result-meta">
              <span className="badge">{top.gameName}</span>
              <span className="score">
                <span
                  className="score-bar"
                  style={{ width: pct(Math.min(1, top.score)) }}
                />
              </span>
              <span className="score-value">{pct(top.score, 1)}</span>
            </div>
            {rest.length > 0 && (
              <ul className="alternatives">
                {rest.slice(0, 3).map((m) => (
                  <li key={`${m.gameName}/${m.cardId}`}>
                    <span>{m.name ?? m.cardId}</span>
                    <span className="dim">{pct(m.score, 1)}</span>
                  </li>
                ))}
              </ul>
            )}
          </>
        ) : (
          <>
            <div className="result-title dim">No match</div>
            <div className="result-meta">
              {card.candidateGames.length ? (
                card.candidateGames.map((g) => (
                  <span className="badge" key={g}>
                    {g}
                  </span>
                ))
              ) : (
                <span className="dim">no candidate game</span>
              )}
            </div>
          </>
        )}
        <div className="result-tags">
          <span className="tag">detection {pct(card.detectionConfidence)}</span>
          <Tags card={card} />
          {confirming && (
            <span className="tag tag-progress">
              confirming {progress.count}/{progress.required}
            </span>
          )}
        </div>
      </div>
    </article>
  );
}

function HistoryRow({
  card,
  onRemove,
}: {
  card: ConfirmedCard;
  onRemove: (cardId: string) => void;
}) {
  return (
    <article className="result result-confirmed">
      <Thumb image={card.thumb} />
      <div className="result-body">
        <div className="result-title">{card.name}</div>
        <div className="result-meta">
          <span className="badge">{card.gameName}</span>
          <span className="score-value">{pct(card.score, 1)}</span>
        </div>
        <div className="result-tags">
          <Tags card={card} />
        </div>
      </div>
      <button
        className="btn btn-icon"
        onClick={() => onRemove(card.cardId)}
        aria-label={`Remove ${card.name}`}
        title="Remove"
      >
        ×
      </button>
    </article>
  );
}

export function Results({
  live,
  progress,
  history,
  onRemove,
  onClear,
}: {
  live: CardView[];
  progress: ConfirmProgress | null;
  history: ConfirmedCard[];
  onRemove: (cardId: string) => void;
  onClear: () => void;
}) {
  return (
    <>
      <section className="panel results">
        <h2>
          Live
          {live.length > 0 && <span className="count">{live.length}</span>}
        </h2>
        {live.length === 0 ? (
          <p className="empty">No cards in the last frame.</p>
        ) : (
          live.map((card, i) => (
            <LiveRow
              key={`${i}:${card.matches[0]?.cardId ?? '?'}`}
              card={card}
              index={i}
              progress={progress}
            />
          ))
        )}
      </section>
      <section className="panel results">
        <h2>
          Confirmed
          {history.length > 0 && (
            <span className="count">{history.length}</span>
          )}
          {history.length > 0 && (
            <button className="btn btn-link" onClick={onClear}>
              clear
            </button>
          )}
        </h2>
        {history.length === 0 ? (
          <p className="empty">
            A card is confirmed after it reads the same on consecutive frames.
          </p>
        ) : (
          history.map((card) => (
            <HistoryRow key={card.cardId} card={card} onRemove={onRemove} />
          ))
        )}
      </section>
    </>
  );
}
