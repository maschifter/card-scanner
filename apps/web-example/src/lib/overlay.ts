import { FRAME_MAX_WIDTH } from '@cardnexus/web-card-scanner';
import type { CardView } from '../hooks/useScanLoop.ts';

export const cardHue = (index: number) => (index * 67) % 360;

export function drawCardsOverlay(
  ctx: CanvasRenderingContext2D,
  cards: readonly CardView[],
): void {
  // Scale strokes and labels with the frame: the canvas is drawn at frame
  // resolution (up to FRAME_MAX_WIDTH) and displayed smaller.
  const k = Math.max(0.75, ctx.canvas.width / FRAME_MAX_WIDTH);
  ctx.lineWidth = 3 * k;
  ctx.lineJoin = 'round';
  ctx.font = `600 ${Math.round(20 * k)}px -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif`;
  cards.forEach((card, i) => {
    const color = `hsl(${cardHue(i)} 80% 60%)`;
    ctx.strokeStyle = color;
    ctx.fillStyle = color;
    const { x1, y1, x2, y2 } = card.box;
    ctx.strokeRect(x1, y1, x2 - x1, y2 - y1);
    const top = card.matches[0];
    const label = top
      ? `${top.name ?? top.cardId}  ${(top.score * 100).toFixed(0)}%`
      : 'no match';
    const padX = 10 * k;
    const h = 30 * k;
    const w = ctx.measureText(label).width + padX * 2;
    const x = Math.max(4, Math.min(card.box.x1, ctx.canvas.width - w - 4));
    const y = Math.max(h + 4, card.box.y1 - 6);
    ctx.beginPath();
    ctx.roundRect(x, y - h, w, h, 6 * k);
    ctx.fill();
    ctx.fillStyle = '#0b0f14';
    ctx.textBaseline = 'middle';
    ctx.fillText(label, x + padX, y - h / 2);
  });
}
