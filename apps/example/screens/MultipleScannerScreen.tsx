import React, { useState } from 'react';
import {
  View,
  Text,
  StyleSheet,
  ScrollView,
  Image,
  ActivityIndicator,
  TouchableOpacity,
} from 'react-native';
import { scanImage, type Detection } from '@cardnexus/card-scanner';
import * as ImagePicker from 'expo-image-picker';
import * as ImageManipulator from 'expo-image-manipulator';
import { useScannerLoader } from '../hooks/useScannerLoader';

// Helper to convert HEIC to JPEG
async function ensureJPEG(uri: string): Promise<string> {
  const isHEIC =
    uri.toLowerCase().endsWith('.heic') ||
    uri.toLowerCase().endsWith('.heif') ||
    uri.includes('.heic') ||
    uri.includes('.heif');

  if (isHEIC) {
    const manipResult = await ImageManipulator.manipulateAsync(uri, [], {
      compress: 1,
      format: ImageManipulator.SaveFormat.JPEG,
    });
    return manipResult.uri;
  }

  return uri;
}

export default function MultipleScannerScreen() {
  const [selectedImage, setSelectedImage] = useState<string | null>(null);
  const [detection, setDetection] = useState<Detection | null>(null);
  const [isProcessing, setIsProcessing] = useState(false);

  const { isLoading, error } = useScannerLoader('multiple');

  const pickImage = async () => {
    const result = await ImagePicker.launchImageLibraryAsync({
      mediaTypes: ['images'],
      allowsEditing: false,
      quality: 1,
    });

    if (!result.canceled && result.assets[0]) {
      const jpegUri = await ensureJPEG(result.assets[0].uri);
      setSelectedImage(jpegUri);
      setDetection(null);
    }
  };

  const runDetection = async () => {
    setIsProcessing(true);
    setDetection(null);

    try {
      const result = await scanImage(selectedImage!);

      setDetection(result);
    } catch (err) {
      const errorMsg = err instanceof Error ? err.message : String(err);
      console.error('Segmentation failed:', errorMsg);
    } finally {
      setIsProcessing(false);
    }
  };

  return (
    <ScrollView
      style={styles.container}
      contentContainerStyle={styles.contentContainer}
    >
      <View style={styles.header}>
        <Text style={styles.title}>Multiple Scanner</Text>
        <Text style={styles.subtitle}>YOLO11 Card Detection</Text>
      </View>

      <View style={styles.buttonContainer}>
        <TouchableOpacity style={styles.primaryButton} onPress={pickImage}>
          <Text style={styles.primaryButtonText}>Pick Image</Text>
        </TouchableOpacity>

        {selectedImage && (
          <TouchableOpacity
            style={[styles.primaryButton, styles.secondaryButton]}
            onPress={runDetection}
            disabled={isProcessing || isLoading}
          >
            <Text style={styles.primaryButtonText}>
              {isProcessing
                ? 'Processing...'
                : isLoading
                  ? 'Loading Models...'
                  : 'Run Segmentation'}
            </Text>
          </TouchableOpacity>
        )}
      </View>

      {selectedImage && (
        <View style={styles.imageCard}>
          <Text style={styles.cardTitle}>Selected Image</Text>
          <Image
            source={{ uri: selectedImage }}
            style={styles.image}
            resizeMode="contain"
          />
        </View>
      )}

      {isProcessing && (
        <View style={styles.loadingCard}>
          <ActivityIndicator size="large" color="#4CAF50" />
          <Text style={styles.loadingText}>Running YOLO segmentation...</Text>
        </View>
      )}

      {error && (
        <View style={styles.errorCard}>
          <Text style={styles.errorIcon}>⚠️</Text>
          <Text style={styles.errorText}>{error}</Text>
        </View>
      )}

      {detection && (
        <>
          <View style={styles.resultCard}>
            <Text style={styles.cardTitle}>Results</Text>
            <View style={styles.statRow}>
              <Text style={styles.statLabel}>Cards Detected:</Text>
              <Text style={styles.statValue}>{detection.cards.length}</Text>
            </View>
          </View>

          {detection.cards.length > 0 && (
            <View style={styles.imageCard}>
              <Text style={styles.cardTitle}>Detected Cards</Text>
              {detection.cards.map((card, index) => (
                <View key={index} style={styles.detectionItem}>
                  <Text style={styles.detectionTitle}>
                    {card.cardId} ({card.gameName.toUpperCase()})
                  </Text>
                  <Text>
                    Confidence: {(card.confidenceScore * 100).toFixed(1)}%
                  </Text>

                  {card.capturedImage && (
                    <Image
                      source={{ uri: card.capturedImage.uri }}
                      style={styles.dewarpedImage}
                      resizeMode="contain"
                    />
                  )}

                  {card.alternativeCards &&
                    card.alternativeCards.length > 0 && (
                      <View style={styles.matchesContainer}>
                        <Text style={styles.matchesTitle}>
                          Alternative Matches:
                        </Text>
                        {card.alternativeCards
                          .slice(0, 3)
                          .map((altCard, matchIndex) => (
                            <View key={matchIndex} style={styles.matchItem}>
                              <Text style={styles.matchName}>
                                {matchIndex + 2}. {altCard.cardId}
                              </Text>
                              <Text style={styles.matchDetails}>
                                Confidence:{' '}
                                {(altCard.confidence * 100).toFixed(1)}%
                              </Text>
                            </View>
                          ))}
                      </View>
                    )}

                  {card.setSymbol && (
                    <View style={styles.setSymbolContainer}>
                      <Text style={styles.setSymbolText}>
                        Set: {card.setSymbol.setCode.toUpperCase()}
                      </Text>
                      <Text style={styles.setSymbolSimilarity}>
                        Similarity:{' '}
                        {(card.setSymbol.similarity * 100).toFixed(1)}%
                      </Text>
                    </View>
                  )}
                </View>
              ))}
            </View>
          )}
        </>
      )}
    </ScrollView>
  );
}

const styles = StyleSheet.create({
  container: {
    flex: 1,
    backgroundColor: '#f5f5f5',
  },
  contentContainer: {
    padding: 20,
  },
  header: {
    alignItems: 'center',
    marginBottom: 20,
    marginTop: 20,
  },
  title: {
    fontSize: 28,
    fontWeight: 'bold',
    color: '#333',
    marginBottom: 8,
  },
  subtitle: {
    fontSize: 16,
    color: '#666',
  },
  buttonContainer: {
    gap: 12,
    marginBottom: 20,
  },
  primaryButton: {
    backgroundColor: '#4CAF50',
    paddingVertical: 16,
    borderRadius: 8,
    alignItems: 'center',
  },
  secondaryButton: {
    backgroundColor: '#2196F3',
  },
  primaryButtonText: {
    color: '#fff',
    fontSize: 16,
    fontWeight: '600',
  },
  imageCard: {
    backgroundColor: '#fff',
    borderRadius: 12,
    padding: 20,
    marginBottom: 20,
    shadowColor: '#000',
    shadowOffset: { width: 0, height: 2 },
    shadowOpacity: 0.1,
    shadowRadius: 4,
    elevation: 3,
  },
  cardTitle: {
    fontSize: 18,
    fontWeight: 'bold',
    color: '#333',
    marginBottom: 12,
  },
  image: {
    width: '100%',
    height: 300,
    borderRadius: 8,
  },
  loadingCard: {
    backgroundColor: '#fff',
    borderRadius: 12,
    padding: 40,
    alignItems: 'center',
    marginBottom: 20,
    shadowColor: '#000',
    shadowOffset: { width: 0, height: 2 },
    shadowOpacity: 0.1,
    shadowRadius: 4,
    elevation: 3,
  },
  loadingText: {
    marginTop: 12,
    fontSize: 14,
    color: '#666',
  },
  errorCard: {
    backgroundColor: '#fff',
    borderRadius: 12,
    padding: 20,
    alignItems: 'center',
    marginBottom: 20,
    shadowColor: '#000',
    shadowOffset: { width: 0, height: 2 },
    shadowOpacity: 0.1,
    shadowRadius: 4,
    elevation: 3,
  },
  errorIcon: {
    fontSize: 40,
    marginBottom: 12,
  },
  errorText: {
    fontSize: 14,
    color: '#d32f2f',
    textAlign: 'center',
  },
  resultCard: {
    backgroundColor: '#fff',
    borderRadius: 12,
    padding: 20,
    marginBottom: 20,
    shadowColor: '#000',
    shadowOffset: { width: 0, height: 2 },
    shadowOpacity: 0.1,
    shadowRadius: 4,
    elevation: 3,
  },
  statRow: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
    paddingVertical: 8,
  },
  statLabel: {
    fontSize: 16,
    color: '#666',
  },
  statValue: {
    fontSize: 18,
    fontWeight: 'bold',
    color: '#333',
  },
  dewarpedImage: {
    width: '100%',
    height: 200,
    borderRadius: 8,
  },
  detectionItem: {
    padding: 16,
    backgroundColor: '#f9f9f9',
    borderRadius: 8,
    marginBottom: 16,
  },
  detectionTitle: {
    fontSize: 18,
    fontWeight: '600',
    color: '#333',
    marginBottom: 12,
  },
  matchesContainer: {
    marginTop: 12,
    padding: 12,
    backgroundColor: '#fff',
    borderRadius: 6,
  },
  matchesTitle: {
    fontSize: 14,
    fontWeight: '600',
    color: '#666',
    marginBottom: 8,
  },
  matchItem: {
    marginBottom: 8,
    paddingBottom: 8,
    borderBottomWidth: 1,
    borderBottomColor: '#eee',
  },
  matchName: {
    fontSize: 15,
    fontWeight: '500',
    color: '#333',
    marginBottom: 4,
  },
  matchDetails: {
    fontSize: 13,
    color: '#666',
  },
  setSymbolContainer: {
    marginTop: 12,
    padding: 10,
    backgroundColor: '#e3f2fd',
    borderRadius: 6,
    borderLeftWidth: 3,
    borderLeftColor: '#2196F3',
  },
  setSymbolText: {
    fontSize: 14,
    fontWeight: '600',
    color: '#1976D2',
    marginBottom: 4,
  },
  setSymbolSimilarity: {
    fontSize: 13,
    color: '#1565C0',
  },
});
