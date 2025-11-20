import React, { useState, useEffect } from 'react';
import {
  View,
  Text,
  StyleSheet,
  ScrollView,
  Image,
  ActivityIndicator,
  TouchableOpacity,
} from 'react-native';
import {
  recognizeCards,
  CardRecognitionResult,
  useDatabaseManager,
  DatabaseInfo,
} from 'react-native-card-scanner';
import * as ImagePicker from 'expo-image-picker';
import * as ImageManipulator from 'expo-image-manipulator';
import { loadModels } from '../utils/models';

// Helper to convert HEIC to JPEG
async function ensureJPEG(uri: string): Promise<string> {
  const isHEIC =
    uri.toLowerCase().endsWith('.heic') ||
    uri.toLowerCase().endsWith('.heif') ||
    uri.includes('.heic') ||
    uri.includes('.heif');

  if (isHEIC) {
    console.log('HEIC image detected, converting to JPEG...');
    const manipResult = await ImageManipulator.manipulateAsync(uri, [], {
      compress: 1,
      format: ImageManipulator.SaveFormat.JPEG,
    });
    console.log('Converted to JPEG:', manipResult.uri);
    return manipResult.uri;
  }

  return uri;
}

export default function RecognitionScreen() {
  const [selectedImage, setSelectedImage] = useState<string | null>(null);
  const [recognitionResult, setRecognitionResult] =
    useState<CardRecognitionResult | null>(null);
  const [isProcessing, setIsProcessing] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const { databases, refreshDatabases } = useDatabaseManager();
  const [selectedGame, setSelectedGame] = useState<string | null>(null);
  const [isDbReady, setIsDbReady] = useState(false);

  useEffect(() => {
    refreshDatabases();
  }, [refreshDatabases]);

  useEffect(() => {
    if (databases.length > 0 && !selectedGame) {
      setSelectedGame(databases[0].gameName);
      setIsDbReady(true);
    } else if (databases.length > 0 && selectedGame) {
      setIsDbReady(true);
    } else {
      setIsDbReady(false);
    }
  }, [databases, selectedGame]);

  const pickImage = async () => {
    const result = await ImagePicker.launchImageLibraryAsync({
      mediaTypes: ['images'],
      allowsEditing: false,
      quality: 1,
    });

    if (!result.canceled && result.assets[0]) {
      const jpegUri = await ensureJPEG(result.assets[0].uri);
      setSelectedImage(jpegUri);
      setRecognitionResult(null);
      setError(null);
    }
  };

  const runRecognition = async () => {
    if (!selectedImage) {
      setError('Please select an image first');
      return;
    }

    setIsProcessing(true);
    setError(null);
    setRecognitionResult(null);

    try {
      console.log('Loading models...');
      const models = await loadModels();
      console.log('Models loaded');

      console.log('Running recognition pipeline...');
      const result = recognizeCards(
        selectedImage,
        models.yolo,
        models.embedding,
        selectedGame,
        0.5, // yoloConf
        0.0, // yoloIou
        3, // topK matches per card
      );

      console.log('Recognition result:', result);
      setRecognitionResult(result);
    } catch (err) {
      const errorMsg = err instanceof Error ? err.message : String(err);
      console.error('Recognition failed:', errorMsg);
      setError(errorMsg);
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
        <Text style={styles.title}>Recognition Demo</Text>
        <Text style={styles.subtitle}>Full Pipeline + Database Search</Text>
      </View>

      <View style={styles.dbControlCard}>
        <Text style={styles.cardTitle}>Select Database</Text>
        <View style={styles.gameButtonGrid}>
          {databases.length > 0 ? (
            databases.map((dbInfo: DatabaseInfo) => (
              <TouchableOpacity
                key={dbInfo.gameName}
                style={[
                  styles.gameButton,
                  dbInfo.gameName === selectedGame && styles.gameButtonActive,
                ]}
                onPress={() => setSelectedGame(dbInfo.gameName)}
                disabled={isProcessing}
              >
                <Text
                  style={[
                    styles.gameButtonText,
                    dbInfo.gameName === selectedGame &&
                      styles.gameButtonTextActive,
                  ]}
                >
                  {dbInfo.gameName}
                </Text>
              </TouchableOpacity>
            ))
          ) : (
            <Text style={styles.loadingText}>
              No databases found. Go to the Databases tab to download one.
            </Text>
          )}
        </View>

        <TouchableOpacity
          style={[
            styles.refreshButton,
            isProcessing && styles.refreshButtonDisabled,
          ]}
          onPress={refreshDatabases}
          disabled={isProcessing}
        >
          <Text style={styles.refreshButtonText}>🔄 Refresh Database List</Text>
        </TouchableOpacity>

        <Text style={styles.sectionSubtitle}>
          Target Game: {selectedGame || 'N/A'} (Ready:{' '}
          {isDbReady ? 'YES' : 'NO'})
        </Text>

        <TouchableOpacity style={styles.primaryButton} onPress={pickImage}>
          <Text style={styles.primaryButtonText}>Pick Image</Text>
        </TouchableOpacity>

        {selectedImage && (
          <TouchableOpacity
            style={[styles.primaryButton, styles.secondaryButton]}
            onPress={runRecognition}
            disabled={isProcessing}
          >
            <Text style={styles.primaryButtonText}>
              {isProcessing ? 'Processing...' : 'Run Recognition'}
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
          <Text style={styles.loadingText}>
            Running recognition pipeline...
          </Text>
          <Text style={styles.loadingSubtext}>This may take a few seconds</Text>
        </View>
      )}

      {error && (
        <View style={styles.errorCard}>
          <Text style={styles.errorIcon}>⚠️</Text>
          <Text style={styles.errorText}>{error}</Text>
        </View>
      )}

      {recognitionResult && (
        <>
          <View style={styles.resultCard}>
            <Text style={styles.cardTitle}>Performance Statistics</Text>
            <View style={styles.statRow}>
              <Text style={styles.statLabel}>Cards Found:</Text>
              <Text style={styles.statValue}>
                {recognitionResult.cards.length}
              </Text>
            </View>

            <Text style={styles.sectionSubtitle}>YOLO Segmentation</Text>
            <View style={styles.statRow}>
              <Text style={styles.statLabel}> • Preprocessing:</Text>
              <Text style={styles.statValue}>
                {recognitionResult.timingBreakdown.yoloPreprocessingMs.toFixed(
                  1,
                )}{' '}
                ms
              </Text>
            </View>
            <View style={styles.statRow}>
              <Text style={styles.statLabel}> • Inference:</Text>
              <Text style={styles.statValue}>
                {recognitionResult.timingBreakdown.yoloInferenceMs.toFixed(1)}{' '}
                ms
              </Text>
            </View>
            <View style={styles.statRow}>
              <Text style={styles.statLabel}> • Postprocessing:</Text>
              <Text style={styles.statValue}>
                {recognitionResult.timingBreakdown.yoloPostprocessingMs.toFixed(
                  1,
                )}{' '}
                ms
              </Text>
            </View>
            <View style={[styles.statRow, styles.totalRow]}>
              <Text style={styles.statLabelBold}> YOLO Total:</Text>
              <Text style={styles.statValueBold}>
                {recognitionResult.timingBreakdown.yoloTotalMs.toFixed(1)} ms
              </Text>
            </View>

            <Text style={styles.sectionSubtitle}>Embedding Extraction</Text>
            <View style={styles.statRow}>
              <Text style={styles.statLabel}> • Preprocessing:</Text>
              <Text style={styles.statValue}>
                {recognitionResult.timingBreakdown.embeddingPreprocessingMs.toFixed(
                  1,
                )}{' '}
                ms
              </Text>
            </View>
            <View style={styles.statRow}>
              <Text style={styles.statLabel}> • Inference:</Text>
              <Text style={styles.statValue}>
                {recognitionResult.timingBreakdown.embeddingInferenceMs.toFixed(
                  1,
                )}{' '}
                ms
              </Text>
            </View>
            <View style={[styles.statRow, styles.totalRow]}>
              <Text style={styles.statLabelBold}> Embedding Total:</Text>
              <Text style={styles.statValueBold}>
                {recognitionResult.timingBreakdown.embeddingTotalMs.toFixed(1)}{' '}
                ms
              </Text>
            </View>

            <Text style={styles.sectionSubtitle}>Database Search</Text>
            <View style={[styles.statRow, styles.totalRow]}>
              <Text style={styles.statLabel}> • Search Time:</Text>
              <Text style={styles.statValue}>
                {recognitionResult.timingBreakdown.databaseSearchMs.toFixed(1)}{' '}
                ms
              </Text>
            </View>

            <View style={[styles.statRow, styles.grandTotalRow]}>
              <Text style={styles.statLabelGrand}>Total Pipeline:</Text>
              <Text style={styles.statValueGrand}>
                {recognitionResult.timingBreakdown.totalPipelineMs.toFixed(1)}{' '}
                ms
              </Text>
            </View>
          </View>

          {recognitionResult.cards.length > 0 ? (
            <View style={styles.resultsContainer}>
              <Text style={styles.sectionTitle}>Recognized Cards</Text>
              {recognitionResult.cards.map((card, index) => (
                <View key={index} style={styles.cardResult}>
                  <View style={styles.cardHeader}>
                    <Text style={styles.cardNumber}>Card {index + 1}</Text>
                    <Text style={styles.cardConfidence}>
                      {(card.conf * 100).toFixed(1)}% confidence
                    </Text>
                  </View>

                  <View style={styles.timingInfo}>
                    <Text style={styles.timingText}>
                      Embedding: {card.embeddingTimeMs.toFixed(1)} ms
                    </Text>
                  </View>

                  {card.matches && card.matches.length > 0 ? (
                    <View style={styles.matchesContainer}>
                      <Text style={styles.matchesTitle}>Top Matches:</Text>
                      {card.matches.slice(0, 10).map((match, matchIndex) => (
                        <View key={matchIndex} style={styles.matchItem}>
                          <View style={styles.matchRank}>
                            <Text style={styles.matchRankText}>
                              #{matchIndex + 1}
                            </Text>
                          </View>
                          <View style={styles.matchInfo}>
                            <Text style={styles.matchName}>{match.name}</Text>
                            <Text style={styles.matchScore}>
                              {(match.score * 100).toFixed(1)}% match
                            </Text>
                          </View>
                        </View>
                      ))}
                    </View>
                  ) : (
                    <Text style={styles.noMatches}>No matches found</Text>
                  )}
                </View>
              ))}
            </View>
          ) : (
            <View style={styles.resultCard}>
              <Text style={styles.noCardsText}>
                No cards detected in the image
              </Text>
            </View>
          )}
        </>
      )}
    </ScrollView>
  );
}

const styles = StyleSheet.create({
  dbControlCard: {
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
  gameButtonGrid: {
    flexDirection: 'row',
    flexWrap: 'wrap',
    gap: 10,
    marginTop: 10,
    marginBottom: 15,
  },
  gameButton: {
    backgroundColor: '#f0f0f0',
    paddingHorizontal: 12,
    paddingVertical: 8,
    borderRadius: 5, // Rounded pill shape
    borderWidth: 2,
    borderColor: '#e0e0e0',
  },
  gameButtonActive: {
    backgroundColor: '#4CAF50',
    borderColor: '#306932',
  },
  gameButtonText: {
    color: '#333',
    fontWeight: '600',
    fontSize: 14,
  },
  gameButtonTextActive: {
    color: '#fff',
    fontWeight: '700',
  },
  refreshButton: {
    backgroundColor: '#607D8B',
    paddingVertical: 10,
    borderRadius: 8,
    alignItems: 'center',
    marginTop: 10,
  },
  refreshButtonText: {
    color: '#fff',
    fontSize: 14,
    fontWeight: '600',
  },
  refreshButtonDisabled: {
    backgroundColor: '#B0BEC5',
  },
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
    fontSize: 16,
    color: '#666',
  },
  loadingSubtext: {
    marginTop: 4,
    fontSize: 14,
    color: '#999',
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
  resultsContainer: {
    marginBottom: 20,
  },
  sectionTitle: {
    fontSize: 20,
    fontWeight: 'bold',
    color: '#333',
    marginBottom: 16,
  },
  cardResult: {
    backgroundColor: '#fff',
    borderRadius: 12,
    padding: 20,
    marginBottom: 16,
    shadowColor: '#000',
    shadowOffset: { width: 0, height: 2 },
    shadowOpacity: 0.1,
    shadowRadius: 4,
    elevation: 3,
  },
  cardHeader: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
    marginBottom: 12,
    paddingBottom: 12,
    borderBottomWidth: 1,
    borderBottomColor: '#eee',
  },
  cardNumber: {
    fontSize: 18,
    fontWeight: 'bold',
    color: '#333',
  },
  cardConfidence: {
    fontSize: 14,
    color: '#4CAF50',
    fontWeight: '600',
  },
  timingInfo: {
    marginBottom: 16,
  },
  timingText: {
    fontSize: 14,
    color: '#666',
  },
  matchesContainer: {
    gap: 12,
  },
  matchesTitle: {
    fontSize: 16,
    fontWeight: '600',
    color: '#333',
    marginBottom: 8,
  },
  matchItem: {
    flexDirection: 'row',
    alignItems: 'center',
    padding: 12,
    backgroundColor: '#f9f9f9',
    borderRadius: 8,
    gap: 12,
  },
  matchRank: {
    width: 32,
    height: 32,
    borderRadius: 16,
    backgroundColor: '#4CAF50',
    justifyContent: 'center',
    alignItems: 'center',
  },
  matchRankText: {
    color: '#fff',
    fontSize: 14,
    fontWeight: 'bold',
  },
  matchInfo: {
    flex: 1,
  },
  matchName: {
    fontSize: 16,
    fontWeight: '600',
    color: '#333',
    marginBottom: 4,
  },
  matchScore: {
    fontSize: 14,
    color: '#666',
  },
  noMatches: {
    fontSize: 14,
    color: '#999',
    fontStyle: 'italic',
    textAlign: 'center',
    paddingVertical: 12,
  },
  noCardsText: {
    fontSize: 16,
    color: '#666',
    textAlign: 'center',
  },
  sectionSubtitle: {
    fontSize: 16,
    fontWeight: '600',
    color: '#4CAF50',
    marginTop: 16,
    marginBottom: 8,
  },
  statLabelBold: {
    fontSize: 16,
    fontWeight: '600',
    color: '#666',
  },
  statValueBold: {
    fontSize: 18,
    fontWeight: 'bold',
    color: '#4CAF50',
  },
  totalRow: {
    borderTopWidth: 1,
    borderTopColor: '#eee',
    paddingTop: 12,
  },
  grandTotalRow: {
    borderTopWidth: 2,
    borderTopColor: '#4CAF50',
    marginTop: 16,
    paddingTop: 12,
  },
  statLabelGrand: {
    fontSize: 18,
    fontWeight: 'bold',
    color: '#333',
  },
  statValueGrand: {
    fontSize: 20,
    fontWeight: 'bold',
    color: '#4CAF50',
  },
});
