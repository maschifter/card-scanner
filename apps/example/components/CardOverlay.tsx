import React, { memo } from 'react';
import { View, Text, StyleSheet } from 'react-native';
import { RecognizedCard } from 'react-native-card-scanner';

interface CardOverlayProps {
  cards: RecognizedCard[];
}

const CardOverlay = memo(function CardOverlay({ cards }: CardOverlayProps) {
  if (!cards || cards.length === 0) {
    return null;
  }

  // Only show the first detected card to avoid React mounting issues
  const card = cards[0];
  const { matches } = card;
  const topMatch = matches && matches.length > 0 ? matches[0] : null;

  if (!topMatch) {
    return null;
  }

  console.log('Rendering overlay for:', topMatch.name);

  return (
    <View style={styles.overlay}>
      <View style={styles.cardInfo}>
        <Text style={styles.cardName} numberOfLines={2}>
          {topMatch.name}
        </Text>
        <Text style={styles.confidence}>
          {(topMatch.score * 100).toFixed(1)}% confident
        </Text>
      </View>
    </View>
  );
});

export default CardOverlay;

const styles = StyleSheet.create({
  overlay: {
    position: 'absolute',
    top: 100,
    left: 20,
    right: 20,
    pointerEvents: 'none',
  },
  cardInfo: {
    backgroundColor: 'rgba(76, 175, 80, 0.95)',
    padding: 16,
    borderRadius: 8,
    marginBottom: 10,
    shadowColor: '#000',
    shadowOffset: { width: 0, height: 2 },
    shadowOpacity: 0.3,
    shadowRadius: 4,
  },
  cardName: {
    color: '#fff',
    fontSize: 20,
    fontWeight: 'bold',
    marginBottom: 4,
  },
  confidence: {
    color: '#fff',
    fontSize: 14,
  },
});
