import React, { useEffect, useState } from 'react';
import {
  Image,
  Pressable,
  ScrollView,
  StyleSheet,
  Text,
  TouchableOpacity,
  View,
  type LayoutChangeEvent,
} from 'react-native';
import Animated, {
  Easing,
  ZoomIn,
  useAnimatedStyle,
  useSharedValue,
  withTiming,
} from 'react-native-reanimated';
import Svg, { Defs, LinearGradient, Rect, Stop } from 'react-native-svg';
import type { AlternativeMatch, DetectedCard } from '@cardnexus/card-scanner';
import type { FrozenCard, FrozenFrame } from '../hooks/useMultiScan';
import { cardLabel } from '../utils/cardNames';
import { fileUri, pct } from '../utils/format';

/** Frame pixels to view points for an image laid out with `cover`, the way
 *  the camera preview fills the same area. */
type Transform = { scale: number; offsetX: number; offsetY: number };

function fitCover(
  frame: { width: number; height: number },
  view: { width: number; height: number },
): Transform {
  const scale = Math.max(view.width / frame.width, view.height / frame.height);
  return {
    scale,
    offsetX: (view.width - frame.width * scale) / 2,
    offsetY: (view.height - frame.height * scale) / 2,
  };
}

/** Where a card sits on the still, in view points. The quad gives the card's
 *  own rotation; a card whose mask produced none squares up to its box. */
function placement(card: DetectedCard, { scale, offsetX, offsetY }: Transform) {
  const b = card.boundingBox;
  const q =
    card.quad?.length === 8
      ? card.quad
      : [b.x1, b.y1, b.x2, b.y1, b.x2, b.y2, b.x1, b.y2];
  const [x0, y0, x1, y1, , , x3, y3] = q;
  return {
    centerX: ((q[0] + q[2] + q[4] + q[6]) / 4) * scale + offsetX,
    centerY: ((q[1] + q[3] + q[5] + q[7]) / 4) * scale + offsetY,
    width: Math.hypot(x1 - x0, y1 - y0) * scale,
    height: Math.hypot(x3 - x0, y3 - y0) * scale,
    angle: Math.atan2(y1 - y0, x1 - x0),
  };
}

/** Every candidate for a card, the current pick first when there is one. */
function candidates(card: DetectedCard): AlternativeMatch[] {
  return card.cardId
    ? [
        {
          cardId: card.cardId,
          confidence: card.confidenceScore ?? 0,
          gameName: card.gameName,
        },
        ...card.alternativeCards,
      ]
    : card.alternativeCards;
}

/** The frozen frame with each card's crop popping in over its spot. */
export function FrozenMultiView({
  frame,
  onReplaceCard,
  onConfirmCard,
  onInspecting,
}: {
  frame: FrozenFrame;
  /** The user picked a different candidate for this slot. */
  onReplaceCard: (index: number, card: DetectedCard) => void;
  /** The user confirmed the slot's current card as is. */
  onConfirmCard: (index: number) => void;
  /** Open or closed, so the screen can give the inspector its full height. */
  onInspecting?: (inspecting: boolean) => void;
}) {
  const [view, setView] = useState({ width: 0, height: 0 });
  const [openIndex, setOpenIndex] = useState<number | null>(null);

  const inspect = (index: number | null) => {
    setOpenIndex(index);
    onInspecting?.(index !== null);
  };

  // A rescan or a screen change unmounts this view; without the cleanup the
  // screen would keep the carousel hidden for good.
  useEffect(() => () => onInspecting?.(false), [onInspecting]);

  const onLayout = (event: LayoutChangeEvent) =>
    setView(event.nativeEvent.layout);
  const transform = fitCover(frame, view);
  const open =
    openIndex === null ? null : (frame.cards[openIndex]?.card ?? null);

  const confirm = () => {
    if (openIndex !== null) {
      onConfirmCard(openIndex);
    }
    inspect(null);
  };

  const pick = (alternative: AlternativeMatch) => {
    if (openIndex === null || !open) {
      return;
    }
    // The current card: keep everything it already knows.
    if (alternative.cardId === open.cardId) {
      confirm();
      return;
    }
    // Set symbol and FAB color described the old match.
    onReplaceCard(openIndex, {
      ...open,
      gameName: alternative.gameName,
      setSymbol: undefined,
      fabColor: undefined,
      cardId: alternative.cardId,
      confidenceScore: alternative.confidence,
      alternativeCards: candidates(open).filter(
        (c) => c.cardId !== alternative.cardId,
      ),
    });
    inspect(null);
  };

  return (
    <View style={StyleSheet.absoluteFill}>
      {/* Framed as the preview was, inset a little so it reads as a capture. */}
      <View style={styles.still} onLayout={onLayout}>
        {frame.uri !== '' && (
          <Image
            source={{ uri: fileUri(frame.uri) }}
            style={StyleSheet.absoluteFill}
            resizeMode="cover"
          />
        )}
        <View style={styles.dim} />
        {view.height > 0 && <ScanSweep height={view.height} />}

        {view.width > 0 &&
          frame.cards.map((slot, index) => (
            <CardOverlay
              key={index}
              slot={slot}
              transform={transform}
              onPress={slot.card ? () => inspect(index) : undefined}
            />
          ))}
      </View>

      {open && (
        <CardInspector
          card={open}
          onPick={pick}
          onConfirm={confirm}
          onClose={() => inspect(null)}
        />
      )}
    </View>
  );
}

const SWEEP_MS = 1100;
const SWEEP_TRAIL = 90;

/** One pass of a flatbed scanner's light down the still: a bright green line
 *  with a fading trail above it. */
function ScanSweep({ height }: { height: number }) {
  const progress = useSharedValue(0);
  useEffect(() => {
    progress.value = withTiming(1, {
      duration: SWEEP_MS,
      easing: Easing.inOut(Easing.quad),
    });
  }, [progress]);

  const style = useAnimatedStyle(() => ({
    transform: [{ translateY: progress.value * height - SWEEP_TRAIL }],
    // Fades out over the last stretch instead of stopping at the edge.
    opacity: progress.value < 0.85 ? 1 : (1 - progress.value) / 0.15,
  }));

  return (
    <Animated.View style={[styles.sweep, style]} pointerEvents="none">
      <Svg width="100%" height={SWEEP_TRAIL}>
        <Defs>
          <LinearGradient id="trail" x1="0" y1="0" x2="0" y2="1">
            <Stop offset="0" stopColor="#4CAF50" stopOpacity="0" />
            <Stop offset="1" stopColor="#4CAF50" stopOpacity="0.45" />
          </LinearGradient>
        </Defs>
        <Rect width="100%" height={SWEEP_TRAIL} fill="url(#trail)" />
      </Svg>
      <View style={styles.sweepLine} />
    </Animated.View>
  );
}

/** One card on the page: a pending outline until recognition fills it. */
function CardOverlay({
  slot,
  transform,
  onPress,
}: {
  slot: FrozenCard;
  transform: Transform;
  onPress?: () => void;
}) {
  const { centerX, centerY, width, height, angle } = placement(
    slot.card ?? slot.placeholder,
    transform,
  );
  const frameStyle = {
    left: centerX - width / 2,
    top: centerY - height / 2,
    width,
    height,
    transform: [{ rotate: `${angle}rad` }],
  };

  if (!slot.card) {
    return <View style={[styles.card, styles.pending, frameStyle]} />;
  }

  const crop = slot.card.capturedImage?.uri;
  return (
    <Animated.View
      entering={ZoomIn.springify().damping(14)}
      style={[styles.card, frameStyle]}
    >
      <Pressable style={styles.fill} onPress={onPress}>
        {crop ? (
          <Image
            source={{ uri: fileUri(crop) }}
            style={styles.fill}
            resizeMode="cover"
          />
        ) : (
          <View style={[styles.fill, styles.unknown]} />
        )}
        <View style={styles.chip}>
          <Text style={styles.chipText} numberOfLines={1}>
            {slot.confirmed ? '✓ ' : ''}
            {slot.card.cardId
              ? `${cardLabel(slot.card.cardId)} ${pct(slot.card.confidenceScore)}`
              : slot.card.alternativeCards[0]
                ? `? ${cardLabel(slot.card.alternativeCards[0].cardId)} ${pct(slot.card.alternativeCards[0].confidence)}`
                : 'Unknown'}
          </Text>
        </View>
      </Pressable>
    </Animated.View>
  );
}

/** The crop the embedder saw, with the numbers behind the verdict. */
function CardInspector({
  card,
  onPick,
  onConfirm,
  onClose,
}: {
  card: DetectedCard;
  onPick: (alternative: AlternativeMatch) => void;
  onConfirm: () => void;
  onClose: () => void;
}) {
  const image = card.capturedImage;
  const matched = Boolean(card.cardId);
  const runnerUp = card.alternativeCards[0];
  const gap =
    matched && runnerUp
      ? (card.confidenceScore ?? 0) - runnerUp.confidence
      : undefined;
  const others = candidates(card);
  // The numbers behind the verdict, as data so the row stays one shape.
  const readings: [string, string][] = [
    ['match', matched ? pct(card.confidenceScore) : 'none'],
    ['detect', pct(card.boundingBox.conf)],
    ['crop', image ? `${image.width}x${image.height}` : '-'],
    ['dewarped', card.quad ? 'yes' : 'box'],
    ['game', card.gameName ?? card.predictedGameName ?? '-'],
    ['gap', gap === undefined ? '-' : pct(gap)],
  ];

  return (
    <View style={styles.inspector}>
      {/* Everything the panel does not claim, so the crop fills it. */}
      <Pressable style={styles.cropArea} onPress={onClose}>
        {image ? (
          <Image
            source={{ uri: fileUri(image.uri) }}
            style={styles.cropImage}
            resizeMode="contain"
          />
        ) : (
          <Text style={styles.readingValue}>no crop saved</Text>
        )}
      </Pressable>

      <View style={styles.panel}>
        <View style={styles.panelHead}>
          <Text style={styles.title} numberOfLines={1}>
            {matched ? cardLabel(card.cardId) : 'Unknown card'}
          </Text>
          {matched && (
            <Text style={styles.confirm} onPress={onConfirm}>
              ✓ Confirm
            </Text>
          )}
          <Text style={styles.close} onPress={onClose}>
            Close
          </Text>
        </View>

        <View style={styles.readings}>
          {readings.map(([label, value]) => (
            <View key={label} style={styles.reading}>
              <Text style={styles.readingLabel}>{label}</Text>
              <Text style={styles.readingValue} numberOfLines={1}>
                {value}
              </Text>
            </View>
          ))}
        </View>

        {others.length > 0 ? (
          <ScrollView style={styles.altList} nestedScrollEnabled>
            {others.map((alternative) => (
              <TouchableOpacity
                key={alternative.cardId}
                style={styles.altRow}
                onPress={() => onPick(alternative)}
              >
                <Text style={styles.altText} numberOfLines={1}>
                  {cardLabel(alternative.cardId)}
                </Text>
                <Text style={styles.altMeta} numberOfLines={1}>
                  {pct(alternative.confidence)} · {alternative.cardId}
                </Text>
              </TouchableOpacity>
            ))}
          </ScrollView>
        ) : (
          <Text style={styles.empty}>
            No database candidate passed the threshold
          </Text>
        )}
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  still: {
    ...StyleSheet.absoluteFillObject,
    margin: 8,
    borderRadius: 12,
    overflow: 'hidden',
    backgroundColor: '#000',
  },
  dim: {
    ...StyleSheet.absoluteFillObject,
    backgroundColor: 'rgba(0,0,0,0.45)',
  },
  sweep: { position: 'absolute', left: 0, right: 0, top: 0 },
  sweepLine: {
    height: 3,
    backgroundColor: '#7CFF8A',
    shadowColor: '#4CAF50',
    shadowOpacity: 0.9,
    shadowRadius: 10,
    shadowOffset: { width: 0, height: 0 },
    elevation: 6,
  },
  card: {
    position: 'absolute',
    borderRadius: 6,
    overflow: 'hidden',
    borderWidth: 2,
    borderColor: '#fff',
    backgroundColor: '#000',
  },
  fill: { width: '100%', height: '100%' },
  pending: {
    backgroundColor: 'rgba(255,255,255,0.15)',
    borderColor: 'rgba(255,255,255,0.4)',
  },
  unknown: { backgroundColor: 'rgba(229,57,53,0.5)' },
  chip: {
    position: 'absolute',
    left: 0,
    right: 0,
    bottom: 0,
    backgroundColor: 'rgba(0,0,0,0.8)',
    paddingVertical: 2,
    paddingHorizontal: 4,
  },
  chipText: { color: '#fff', fontSize: 9, fontWeight: '600' },

  inspector: {
    ...StyleSheet.absoluteFillObject,
    backgroundColor: 'rgba(0,0,0,0.92)',
  },
  cropArea: {
    flex: 1,
    padding: 12,
    alignItems: 'center',
    justifyContent: 'center',
  },
  cropImage: { width: '100%', height: '100%' },
  panel: {
    backgroundColor: '#1e1e1e',
    paddingHorizontal: 16,
    paddingTop: 10,
    paddingBottom: 12,
    borderTopLeftRadius: 12,
    borderTopRightRadius: 12,
  },
  panelHead: { flexDirection: 'row', alignItems: 'center' },
  title: { color: '#fff', fontSize: 16, fontWeight: '600', flex: 1 },
  confirm: {
    color: '#fff',
    backgroundColor: '#4CAF50',
    fontSize: 14,
    fontWeight: '600',
    paddingVertical: 4,
    paddingHorizontal: 10,
    borderRadius: 6,
    marginRight: 8,
    overflow: 'hidden',
  },
  close: { color: '#4CAF50', fontSize: 14, fontWeight: '600', padding: 4 },
  readings: { flexDirection: 'row', marginTop: 8, marginBottom: 6 },
  reading: { flex: 1 },
  readingLabel: { color: '#8a8a8a', fontSize: 9, textTransform: 'uppercase' },
  readingValue: { color: '#fff', fontSize: 12, fontWeight: '600' },
  // Two rows tall; the rest scrolls.
  altList: { height: 76 },
  altRow: {
    paddingVertical: 5,
    borderBottomWidth: 1,
    borderBottomColor: '#333',
  },
  altMeta: { color: '#999', fontSize: 11 },
  empty: { color: '#999', fontSize: 12, paddingVertical: 10 },
  altText: { color: '#fff', fontSize: 14 },
});
