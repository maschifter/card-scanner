import React, { useEffect, useState } from 'react';
import {
  View,
  Text,
  StyleSheet,
  ScrollView,
  Image,
  ActivityIndicator,
  TouchableOpacity,
} from 'react-native';
import { initializeScanner, releaseScanner } from 'react-native-card-scanner';
import * as ImagePicker from 'expo-image-picker';
import { Asset } from 'expo-asset';
import { cacheDirectory, copyAsync } from 'expo-file-system/legacy';
import * as ImageManipulator from 'expo-image-manipulator';

export default function SetSymbolDetectionScreen() {
  const [selectedImage, setSelectedImage] = useState<string | null>(null);
  const [isProcessing, setIsProcessing] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [isLoadingModels, setIsLoadingModels] = useState(true);
  const [result, setResult] = useState<any>(null);

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

  useEffect(() => {
    loadModels();

    return () => {
      releaseScanner();
    };
  }, []);

  const loadModels = async () => {
    try {
      setIsLoadingModels(true);

      console.log('📦 Loading set symbol detection models...');

      // Load set symbol YOLO model
      const yoloAsset = Asset.fromModule(
        require('../assets/mtg/set_symbol_detection.pte'),
      );
      await yoloAsset.downloadAsync();
      if (!yoloAsset.localUri) {
        throw new Error('Failed to load set symbol YOLO model');
      }
      const yoloLocalPath = `${cacheDirectory}set_symbol_detection.pte`;
      await copyAsync({ from: yoloAsset.localUri, to: yoloLocalPath });
      console.log('✅ Set symbol YOLO loaded');

      // Load set symbol embedder
      const embedderAsset = Asset.fromModule(
        require('../assets/mtg/set_symbol_embedder.pte'),
      );
      await embedderAsset.downloadAsync();
      if (!embedderAsset.localUri) {
        throw new Error('Failed to load set symbol embedder');
      }
      const embedderLocalPath = `${cacheDirectory}set_symbol_embedder.pte`;
      await copyAsync({ from: embedderAsset.localUri, to: embedderLocalPath });
      console.log('✅ Set symbol embedder loaded');

      // Load card models (required for initialization)
      const cardYoloAsset = Asset.fromModule(
        require('../assets/yolo11n-seg-cls.pte'),
      );
      await cardYoloAsset.downloadAsync();
      if (!cardYoloAsset.localUri) {
        throw new Error('Failed to load card YOLO model');
      }
      const cardYoloLocalPath = `${cacheDirectory}yolo11n-seg-cls.pte`;
      await copyAsync({ from: cardYoloAsset.localUri, to: cardYoloLocalPath });

      const cardEmbeddingAsset = Asset.fromModule(
        require('../assets/embedding_model.pte'),
      );
      await cardEmbeddingAsset.downloadAsync();
      if (!cardEmbeddingAsset.localUri) {
        throw new Error('Failed to load card embedding model');
      }
      const cardEmbeddingLocalPath = `${cacheDirectory}embedding_model.pte`;
      await copyAsync({
        from: cardEmbeddingAsset.localUri,
        to: cardEmbeddingLocalPath,
      });

      // Initialize scanner with set symbol detection
      console.log('🚀 Initializing scanner with set symbol detection...');
      const initResult = await initializeScanner({
        segmentationModelPath: cardYoloLocalPath,
        embeddingModelPath: cardEmbeddingLocalPath,
        gameName: 'mtg',
        scanMode: 'single',
        segmentationThreshold: 0.7,
        iouThreshold: 0.7,
        confidenceThreshold: 0.6,
        maxMatches: 5,
        searchCandidates: 100,
        captureImage: false,
      });

      if (!initResult.success) {
        throw new Error(initResult.error || 'Failed to initialize scanner');
      }

      console.log('✅ Scanner initialized with set symbol detection');
      setIsLoadingModels(false);
    } catch (error) {
      console.error('❌ Failed to initialize:', error);
      setError(error instanceof Error ? error.message : String(error));
      setIsLoadingModels(false);
    }
  };

  const pickImage = async () => {
    const result = await ImagePicker.launchImageLibraryAsync({
      mediaTypes: ['images'],
      allowsEditing: false,
      quality: 1,
    });

    if (!result.canceled && result.assets[0]) {
      const jpegUri = await ensureJPEG(result.assets[0].uri);
      console.log(jpegUri);
      setSelectedImage(jpegUri);
      setResult(null);
      setError(null);
    }
  };

  const detectSetSymbol = async () => {
    if (!selectedImage) {
      setError('Please select an image first');
      return;
    }

    setIsProcessing(true);
    setError(null);
    setResult(null);

    try {
      console.log('🔍 Detecting set symbol...');
      const detectionResult = global.detectSetSymbol(selectedImage);

      console.log('Detection result:', detectionResult);

      if (!detectionResult.success) {
        setError(detectionResult.error || 'Detection failed');
        return;
      }

      setResult(detectionResult);
    } catch (err) {
      const errorMsg = err instanceof Error ? err.message : String(err);
      console.error('Detection failed:', errorMsg);
      setError(errorMsg);
    } finally {
      setIsProcessing(false);
    }
  };

  if (isLoadingModels) {
    return (
      <View style={styles.container}>
        <ActivityIndicator size="large" color="#4CAF50" />
        <Text style={styles.message}>Loading models...</Text>
      </View>
    );
  }

  return (
    <ScrollView
      style={styles.container}
      contentContainerStyle={styles.contentContainer}
    >
      <View style={styles.header}>
        <Text style={styles.title}>Set Symbol Detection</Text>
        <Text style={styles.subtitle}>MTG Set Recognition</Text>
      </View>

      {error && (
        <View style={styles.errorCard}>
          <Text style={styles.errorIcon}>⚠️</Text>
          <Text style={styles.errorText}>{error}</Text>
        </View>
      )}

      <View style={styles.buttonContainer}>
        <TouchableOpacity style={styles.primaryButton} onPress={pickImage}>
          <Text style={styles.primaryButtonText}>Pick MTG Card Image</Text>
        </TouchableOpacity>

        {selectedImage && (
          <TouchableOpacity
            style={[styles.primaryButton, styles.secondaryButton]}
            onPress={detectSetSymbol}
            disabled={isProcessing}
          >
            <Text style={styles.primaryButtonText}>
              {isProcessing ? 'Processing...' : 'Detect Set Symbol'}
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

      {result && (
        <>
          {result.setCode && (
            <View style={styles.resultCard}>
              <Text style={styles.cardTitle}>✅ Identified Set</Text>
              <Text style={styles.resultText}>
                Set: {result.setCode.toUpperCase()} - {result.setName}
              </Text>
              <Text style={styles.resultText}>Variant: {result.variant}</Text>
              <Text style={styles.resultText}>
                Similarity: {((result.confidence || 0) * 100).toFixed(1)}%
              </Text>
            </View>
          )}

          {result.croppedImagePath && (
            <View style={styles.imageCard}>
              <Text style={styles.cardTitle}>🎯 Extracted Set Symbol</Text>
              <Image
                source={{ uri: result.croppedImagePath }}
                style={styles.symbolImage}
                resizeMode="contain"
              />
            </View>
          )}

          {result.topMatches && result.topMatches.length > 0 && (
            <View style={styles.resultCard}>
              <Text style={styles.cardTitle}>🏆 Top 5 Matches</Text>
              {result.topMatches.map((match, idx) => (
                <View key={idx} style={styles.matchItem}>
                  <Text style={styles.matchRank}>#{idx + 1}</Text>
                  <View style={styles.matchInfo}>
                    <Text style={styles.matchText}>
                      {match.setCode.toUpperCase()} - {match.setName}
                    </Text>
                    <Text style={styles.matchVariant}>{match.variant}</Text>
                    <Text style={styles.matchSimilarity}>
                      {(match.similarity * 100).toFixed(1)}%
                    </Text>
                  </View>
                </View>
              ))}
            </View>
          )}

          {result.bbox && (
            <View style={styles.resultCard}>
              <Text style={styles.cardTitle}>📍 Detection Info</Text>
              <Text style={styles.resultText}>
                Location: ({Math.round(result.bbox.x1)},{' '}
                {Math.round(result.bbox.y1)}) → ({Math.round(result.bbox.x2)},{' '}
                {Math.round(result.bbox.y2)})
              </Text>
              <Text style={styles.resultText}>
                Confidence: {(result.bbox.confidence * 100).toFixed(1)}%
              </Text>
            </View>
          )}

          {result.embedding && (
            <View style={styles.resultCard}>
              <Text style={styles.cardTitle}>
                🧬 Embedding Vector (128-dim)
              </Text>
              <Text style={styles.embeddingInfo}>
                L2-normalized {result.embedding.length}-dimensional vector
              </Text>
              <ScrollView
                horizontal
                style={styles.embeddingScroll}
                contentContainerStyle={styles.embeddingContainer}
              >
                {result.embedding.map((value, idx) => (
                  <View key={idx} style={styles.embeddingItem}>
                    <Text style={styles.embeddingIndex}>{idx}</Text>
                    <Text style={styles.embeddingValue}>
                      {value.toFixed(4)}
                    </Text>
                  </View>
                ))}
              </ScrollView>
              <Text style={styles.embeddingStats}>
                Min: {Math.min(...result.embedding).toFixed(4)} | Max:{' '}
                {Math.max(...result.embedding).toFixed(4)} | Mean:{' '}
                {(
                  result.embedding.reduce((a, b) => a + b, 0) /
                  result.embedding.length
                ).toFixed(4)}
              </Text>
            </View>
          )}

          {result.performance && (
            <View style={styles.resultCard}>
              <Text style={styles.cardTitle}>⏱️ Performance</Text>
              <Text style={styles.resultText}>
                YOLO Detection: {result.performance.detectionMs.toFixed(1)}ms
              </Text>
              <Text style={styles.resultText}>
                Embedding: {result.performance.embeddingMs.toFixed(1)}ms
              </Text>
              <Text style={styles.resultText}>
                Total:{' '}
                {(
                  result.performance.detectionMs +
                  result.performance.embeddingMs
                ).toFixed(1)}
                ms
              </Text>
            </View>
          )}
        </>
      )}

      <View style={styles.infoCard}>
        <Text style={styles.infoTitle}>ℹ️ About Set Symbol Detection</Text>
        <Text style={styles.infoText}>This demo uses a 2-stage pipeline:</Text>
        <Text style={styles.infoText}>
          1. YOLO detects set symbol location (384x384)
        </Text>
        <Text style={styles.infoText}>
          2. Embedder identifies the set (96x96, 128-dim)
        </Text>
        <Text style={styles.infoText}>
          Database: 816 set symbols from 272 MTG sets
        </Text>
      </View>
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
  message: {
    textAlign: 'center',
    marginTop: 12,
    color: '#666',
    fontSize: 16,
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
  symbolImage: {
    width: '100%',
    height: 150,
    borderRadius: 8,
    backgroundColor: '#f0f0f0',
  },
  embeddingInfo: {
    fontSize: 14,
    color: '#666',
    marginBottom: 12,
    fontStyle: 'italic',
  },
  embeddingScroll: {
    maxHeight: 120,
  },
  embeddingContainer: {
    flexDirection: 'row',
    gap: 8,
    paddingVertical: 8,
  },
  embeddingItem: {
    backgroundColor: '#f5f5f5',
    borderRadius: 6,
    padding: 8,
    minWidth: 70,
    alignItems: 'center',
  },
  embeddingIndex: {
    fontSize: 10,
    color: '#999',
    marginBottom: 4,
  },
  embeddingValue: {
    fontSize: 12,
    color: '#333',
    fontFamily: 'monospace',
  },
  embeddingStats: {
    fontSize: 12,
    color: '#666',
    marginTop: 12,
    textAlign: 'center',
    fontFamily: 'monospace',
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
  resultText: {
    fontSize: 16,
    color: '#333',
    marginBottom: 8,
  },
  matchItem: {
    flexDirection: 'row',
    alignItems: 'center',
    paddingVertical: 8,
    borderBottomWidth: 1,
    borderBottomColor: '#eee',
  },
  matchRank: {
    fontSize: 18,
    fontWeight: 'bold',
    color: '#666',
    marginRight: 12,
    width: 30,
  },
  matchInfo: {
    flex: 1,
  },
  matchText: {
    fontSize: 14,
    color: '#333',
    fontWeight: '500',
  },
  matchVariant: {
    fontSize: 12,
    color: '#666',
    marginTop: 2,
  },
  matchSimilarity: {
    fontSize: 14,
    color: '#4CAF50',
    fontWeight: '600',
    marginTop: 2,
  },
  infoCard: {
    backgroundColor: '#E3F2FD',
    borderRadius: 12,
    padding: 20,
    marginBottom: 20,
  },
  infoTitle: {
    fontSize: 16,
    fontWeight: 'bold',
    color: '#1976D2',
    marginBottom: 12,
  },
  infoText: {
    fontSize: 14,
    color: '#555',
    marginBottom: 6,
  },
});
