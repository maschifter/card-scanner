import React, { useState, useEffect } from 'react';
import {
  View,
  Text,
  Button,
  StyleSheet,
  Platform,
  ScrollView,
  Image,
} from 'react-native';
import {
  runInference,
  runInferenceOnImage,
  InferenceResult,
  loadCardEmbeddings,
  searchSimilarCards,
  CardSearchResult,
  LoadEmbeddingsResult,
  recognizeCards,
  CardRecognitionResult,
  runYoloSegmentation,
  YoloSegmentationResult,
  useDatabaseManager,
} from 'react-native-card-scanner';
import {
  cacheDirectory,
  copyAsync,
  documentDirectory,
  readAsStringAsync,
  writeAsStringAsync,
  getInfoAsync,
} from 'expo-file-system/legacy';
import { Asset } from 'expo-asset';
import * as ImagePicker from 'expo-image-picker';
import * as DocumentPicker from 'expo-document-picker';
import * as ImageManipulator from 'expo-image-manipulator';

declare global {
  function runTests(): string;
}

// Helper function to ensure image is in JPEG format (not HEIC)
async function ensureJPEG(uri: string): Promise<string> {
  // Check if the image is HEIC/HEIF format
  const isHEIC =
    uri.toLowerCase().endsWith('.heic') ||
    uri.toLowerCase().endsWith('.heif') ||
    uri.includes('.heic') ||
    uri.includes('.heif');

  if (isHEIC) {
    console.log('⚠️ HEIC image detected, converting to JPEG...');
    // Convert to JPEG using image manipulator
    const manipResult = await ImageManipulator.manipulateAsync(
      uri,
      [], // No transformations, just format conversion
      { compress: 1, format: ImageManipulator.SaveFormat.JPEG },
    );
    console.log('✅ Converted to JPEG:', manipResult.uri);
    return manipResult.uri;
  }

  return uri;
}

export default function MainScreen() {
  const [result, setResult] = useState<InferenceResult | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [testResult, setTestResult] = useState<string | null>(null);
  const [loadResult, setLoadResult] = useState<LoadEmbeddingsResult | null>(
    null,
  );
  const [searchResults, setSearchResults] = useState<CardSearchResult[]>([]);
  const [selectedImage, setSelectedImage] = useState<string | null>(null);
  const [recognitionResult, setRecognitionResult] =
    useState<CardRecognitionResult | null>(null);
  const [yoloResult, setYoloResult] = useState<YoloSegmentationResult | null>(
    null,
  );
  const [swapResult, setSwapResult] = useState<string | null>(null);

  const handleRunTests = () => {
    try {
      console.log('Running C++ tests...');
      const result = runTests();
      setTestResult(result);
      console.log('C++ test result:', result);
    } catch (e) {
      const errorMsg = e instanceof Error ? e.message : String(e);
      setTestResult(`Error: ${errorMsg}`);
      console.error('Error running tests:', errorMsg);
    }
  };

  const handleLoadEmbeddings = async () => {
    setError(null);
    setLoadResult(null);

    try {
      // Pick JSON file from storage using document picker
      const result = await DocumentPicker.getDocumentAsync({
        type: 'application/json',
        copyToCacheDirectory: true,
      });

      if (result.canceled) {
        return;
      }

      const jsonUri = result.assets[0].uri;
      console.log('Selected JSON file:', jsonUri);
      console.log('File name:', result.assets[0].name);
      console.log(
        'File size:',
        (result.assets[0].size! / 1024 / 1024).toFixed(2),
        'MB',
      );

      const dbPath = `${documentDirectory}objectbox-cards`;
      console.log('Database path:', dbPath);
      console.log('Loading embeddings from:', jsonUri);

      const loadResult = loadCardEmbeddings(dbPath, jsonUri);
      setLoadResult(loadResult);
      console.log('✅ Load result:', loadResult);
    } catch (err) {
      const errorMsg = err instanceof Error ? err.message : String(err);
      setError(`Failed to load embeddings: ${errorMsg}`);
      console.error('Error loading embeddings:', errorMsg);
    }
  };

  const handleSearchSimilar = () => {
    setError(null);
    setSearchResults([]);

    try {
      if (!result || !result.embedding || result.embedding.length !== 256) {
        setError('Please run inference first to get an embedding');
        return;
      }

      const dbPath = `${documentDirectory}objectbox-cards`;
      console.log('Searching for similar cards...');
      console.log(result.embedding);
      const results = searchSimilarCards(dbPath, result.embedding, 10);
      setSearchResults(results);
      console.log('Found', results.length, 'similar cards');
    } catch (err) {
      const errorMsg = err instanceof Error ? err.message : String(err);
      setError(`Failed to search: ${errorMsg}`);
      console.error('Error searching:', errorMsg);
    }
  };

  const handlePickImage = async () => {
    setError(null);
    setResult(null);
    setSearchResults([]);

    try {
      // Request permission
      const { status } =
        await ImagePicker.requestMediaLibraryPermissionsAsync();
      if (status !== 'granted') {
        setError('Permission to access media library is required!');
        return;
      }

      // Pick image
      const result = await ImagePicker.launchImageLibraryAsync({
        mediaTypes: ImagePicker.MediaTypeOptions.Images,
        quality: 1,
      });

      if (result.canceled) {
        return;
      }

      let imageUri = result.assets[0].uri;
      console.log('Selected image:', imageUri);

      // Convert HEIC to JPEG if needed
      imageUri = await ensureJPEG(imageUri);
      setSelectedImage(imageUri);

      // Load the model
      const assetPath = 'embedding_model.pte';
      const [asset] = await Asset.loadAsync(
        require('../assets/embedding_model.pte'),
      );

      let modelPath: string;

      if (Platform.OS === 'android') {
        modelPath = asset.localUri || asset.uri;
      } else {
        const cachePath = `${cacheDirectory}${assetPath}`;
        await copyAsync({
          from: asset.localUri || asset.uri,
          to: cachePath,
        });
        modelPath = cachePath;
      }

      console.log('Processing image with model...');

      // Run inference on the selected image
      const inferenceResult = runInferenceOnImage(modelPath, imageUri);
      setResult(inferenceResult);

      console.log('✅ Inference complete!');
      console.log('Output shape:', inferenceResult.outputShape);
      console.log('Inference time:', inferenceResult.inferenceTimeMs, 'ms');
      console.log(
        'Embedding extracted:',
        inferenceResult.embedding.length,
        'dimensions',
      );
    } catch (err) {
      const errorMsg = err instanceof Error ? err.message : String(err);
      setError(errorMsg);
      console.error('Error picking image:', errorMsg);
    }
  };

  const handleRunYolo = async () => {
    setError(null);
    setYoloResult(null);

    try {
      // Request permission
      const { status } =
        await ImagePicker.requestMediaLibraryPermissionsAsync();
      if (status !== 'granted') {
        setError('Permission to access media library is required!');
        return;
      }

      // Pick image
      const result = await ImagePicker.launchImageLibraryAsync({
        mediaTypes: ImagePicker.MediaTypeOptions.Images,
        quality: 1,
      });

      if (result.canceled) {
        return;
      }

      let imageUri = result.assets[0].uri;
      console.log('Selected image:', imageUri);

      // Convert HEIC to JPEG if needed
      imageUri = await ensureJPEG(imageUri);
      setSelectedImage(imageUri);

      // Load YOLO model
      const yoloAsset = await Asset.loadAsync(
        require('../assets/yolo11n-seg.pte'),
      );
      let yoloModelPath: string;
      if (Platform.OS === 'android') {
        yoloModelPath = yoloAsset[0].localUri || yoloAsset[0].uri;
      } else {
        const cachePath = `${cacheDirectory}yolo11n-seg.pte`;
        await copyAsync({
          from: yoloAsset[0].localUri || yoloAsset[0].uri,
          to: cachePath,
        });
        yoloModelPath = cachePath;
      }

      console.log('🔍 Running YOLO segmentation...');
      console.log('YOLO model:', yoloModelPath);
      console.log('Cache directory:', cacheDirectory);

      // Run YOLO
      const segResult = runYoloSegmentation(
        yoloModelPath,
        imageUri,
        0.5, // confidence
        0.0, // IoU
      );

      console.log('YOLO result paths:');
      console.log('- Visualized:', segResult.visualizedImagePath);
      console.log('- Dewarped cards:', segResult.dewarpedCardPaths);

      setYoloResult(segResult);

      console.log('✅ YOLO complete!');
      console.log('Inference time:', segResult.inferenceTimeMs, 'ms');
      console.log('Detected cards:', segResult.detections.length);
    } catch (err) {
      const errorMsg = err instanceof Error ? err.message : String(err);
      setError(errorMsg);
      console.error('Error running YOLO:', errorMsg);
    }
  };

  const handleRecognizeCards = async () => {
    setError(null);
    setRecognitionResult(null);
    setYoloResult(null);

    const jsStart = performance.now();

    try {
      // Request permission
      const { status } =
        await ImagePicker.requestMediaLibraryPermissionsAsync();
      if (status !== 'granted') {
        setError('Permission to access media library is required!');
        return;
      }

      // Pick image
      const result = await ImagePicker.launchImageLibraryAsync({
        mediaTypes: ImagePicker.MediaTypeOptions.Images,
        quality: 1,
      });

      if (result.canceled) {
        return;
      }

      let imageUri = result.assets[0].uri;
      console.log('Selected image:', imageUri);

      // Convert HEIC to JPEG if needed
      const heicStart = performance.now();
      imageUri = await ensureJPEG(imageUri);
      const heicEnd = performance.now();
      const heicTime = heicEnd - heicStart;
      console.log(`⏱️ HEIC conversion (if any): ${heicTime.toFixed(1)}ms`);
      setSelectedImage(imageUri);

      // Load models
      const assetStart = performance.now();
      const yoloAsset = await Asset.loadAsync(
        require('../assets/yolo11n-seg.pte'),
      );
      const embeddingAsset = await Asset.loadAsync(
        require('../assets/embedding_model.pte'),
      );
      const assetEnd = performance.now();
      console.log(`⏱️ Asset loading: ${(assetEnd - assetStart).toFixed(1)}ms`);

      let yoloModelPath: string;
      let embeddingModelPath: string;

      if (Platform.OS === 'android') {
        yoloModelPath = yoloAsset[0].localUri || yoloAsset[0].uri;
        embeddingModelPath =
          embeddingAsset[0].localUri || embeddingAsset[0].uri;
      } else {
        const yoloCachePath = `${cacheDirectory}yolo11n-seg.pte`;
        const embeddingCachePath = `${cacheDirectory}embedding_model.pte`;

        await copyAsync({
          from: yoloAsset[0].localUri || yoloAsset[0].uri,
          to: yoloCachePath,
        });
        await copyAsync({
          from: embeddingAsset[0].localUri || embeddingAsset[0].uri,
          to: embeddingCachePath,
        });

        yoloModelPath = yoloCachePath;
        embeddingModelPath = embeddingCachePath;
      }

      const dbPath = `${documentDirectory}objectbox-cards`;

      console.log('🚀 Running complete pipeline...');
      console.log('YOLO model:', yoloModelPath);
      console.log('Embedding model:', embeddingModelPath);
      console.log('Database:', dbPath);

      // Run complete pipeline
      const pipelineResult = recognizeCards(
        imageUri,
        yoloModelPath,
        embeddingModelPath,
        dbPath,
        0.5, // yoloConf
        0.0, // yoloIou
        3, // topK
      );

      setRecognitionResult(pipelineResult);

      console.log('✅ Pipeline complete!');
      console.log('YOLO time:', pipelineResult.yoloTimeMs, 'ms');
      console.log('Total time:', pipelineResult.totalTimeMs, 'ms');
      console.log('Recognized cards:', pipelineResult.cards.length);
    } catch (err) {
      const errorMsg = err instanceof Error ? err.message : String(err);
      setError(errorMsg);
      console.error('Error running pipeline:', errorMsg);
    }
  };

  const {
    downloadAndSwap,
    isDownloading,
    downloadError,
    downloadSuccess,
    databases,
  } = useDatabaseManager(documentDirectory || '');

  const handleDownloadAndSwap = async () => {
    setError(null);
    setSwapResult(null);
    const dbName = 'lorocana';
    const downloadUrl = 'file:///Users/bartlomiejobrochta/Downloads/data.mdb';
    const success = await downloadAndSwap(dbName, downloadUrl);
    if (success) {
      setSwapResult(downloadSuccess);
    } else {
      setError(downloadError);
    }
  };

  return (
    <ScrollView
      style={styles.scrollView}
      contentContainerStyle={styles.container}
    >
      <Text style={styles.title}>Card Scanner Example</Text>
      <Text style={styles.subtitle}>TCG Card Recognition with Embeddings</Text>

      <Button
        title="🚀 Recognize Cards (Full Pipeline)"
        onPress={handleRecognizeCards}
        color="#9C27B0"
      />
      <View style={styles.separator} />

      <Button
        title="🎯 Test YOLO Segmentation"
        onPress={handleRunYolo}
        color="#4CAF50"
      />
      <View style={styles.separator} />

      <Button
        title="📷 Pick Image from Gallery"
        onPress={handlePickImage}
        color="#FF6B6B"
      />
      <View style={styles.separator} />

      {selectedImage && (
        <View style={styles.imageContainer}>
          <Text style={styles.sectionTitle}>Selected Image:</Text>
          <Image source={{ uri: selectedImage }} style={styles.selectedImage} />
        </View>
      )}

      <Button
        title="📁 Load Card Embeddings (Select JSON)"
        onPress={handleLoadEmbeddings}
        color="#FF9800"
      />
      <View style={styles.separator} />
      <Button
        title="Search Similar Cards"
        onPress={handleSearchSimilar}
        disabled={!result || !result.embedding}
      />
      <View style={styles.separator} />
      <Button
        title="Download & Swap DB"
        onPress={handleDownloadAndSwap}
        disabled={isDownloading}
      />
      <View style={styles.separator} />
      <Button title="Run C++ Tests" onPress={handleRunTests} />

      {isDownloading && (
        <Text style={styles.info}>Downloading and swapping database...</Text>
      )}
      {downloadSuccess && <Text style={styles.result}>{downloadSuccess}</Text>}

      {result !== null && (
        <>
          <Text style={styles.result}>
            Output shape: [{result.outputShape.join(', ')}]
          </Text>
          <Text style={styles.timing}>
            Inference time: {result.inferenceTimeMs.toFixed(2)} ms
          </Text>
          <Text style={styles.info}>
            Embedding: {result.embedding.length}D vector extracted
          </Text>
        </>
      )}

      {loadResult && (
        <View style={styles.resultBox}>
          <Text style={styles.result}>Loaded {loadResult.loaded} cards</Text>
          <Text style={styles.info}>
            Total cards in DB: {loadResult.totalCards}
          </Text>
        </View>
      )}

      {searchResults.length > 0 && (
        <View style={styles.resultBox}>
          <Text style={styles.sectionTitle}>Top 10 Similar Cards:</Text>
          {searchResults.map((card, index) => (
            <View key={index} style={styles.cardResult}>
              <Text style={styles.cardRank}>#{index + 1}</Text>
              <View style={styles.cardInfo}>
                <Text style={styles.cardName}>{card.name}</Text>
                <Text style={styles.cardId}>ID: {card.cardId}</Text>
              </View>
              <Text style={styles.cardScore}>
                {(card.score * 100).toFixed(1)}%
              </Text>
            </View>
          ))}
        </View>
      )}

      {databases.length > 0 && (
        <View style={styles.resultBox}>
          <Text style={styles.sectionTitle}>Available Databases:</Text>
          <View style={styles.tableHeader}>
            <Text style={styles.tableHeaderText}>Game Name</Text>
            <Text style={styles.tableHeaderText}>Path</Text>
          </View>
          {databases.map((db, index) => (
            <View key={index} style={styles.tableRow}>
              <Text style={styles.tableCell}>{db.gameName}</Text>
              <Text style={styles.tableCell}>{db.path}</Text>
            </View>
          ))}
        </View>
      )}

      {yoloResult && (
        <View style={styles.resultBox}>
          <Text style={styles.sectionTitle}>🎯 YOLO Segmentation Results</Text>
          <Text style={styles.info}>
            Inference time: {yoloResult.inferenceTimeMs.toFixed(1)}ms
          </Text>
          <Text style={styles.timing}>
            Found {yoloResult.detections.length} card
            {yoloResult.detections.length !== 1 ? 's' : ''}
          </Text>

          {yoloResult.visualizedImagePath && (
            <View style={styles.imageContainer}>
              <Text style={styles.sectionTitle}>
                Detected Cards with Overlays:
              </Text>
              <Image
                source={{ uri: `file://${yoloResult.visualizedImagePath}` }}
                style={styles.selectedImage}
                resizeMode="contain"
              />
            </View>
          )}

          <Text style={styles.sectionTitle}>Dewarped Cards:</Text>
          <View style={styles.cardGrid}>
            {yoloResult.dewarpedCardPaths &&
              yoloResult.dewarpedCardPaths.map((cardPath, index) => (
                <View key={index} style={styles.detectedCard}>
                  <Text style={styles.detectedCardTitle}>
                    Card #{index + 1} (
                    {(yoloResult.detections[index].box.conf * 100).toFixed(1)}%)
                  </Text>
                  {cardPath && (
                    <Image
                      source={{ uri: `file://${cardPath}` }}
                      style={styles.dewarpedCard}
                      resizeMode="contain"
                    />
                  )}
                  {!cardPath && (
                    <Text style={styles.info}>
                      ⚠️ Could not extract quadrilateral
                    </Text>
                  )}
                </View>
              ))}
          </View>
        </View>
      )}

      {recognitionResult && (
        <View style={styles.resultBox}>
          <Text style={styles.sectionTitle}>🚀 Card Recognition Results</Text>

          {/* Timing Breakdown */}
          <View style={styles.timingBox}>
            <Text style={styles.timingTitle}>⏱️ Performance Breakdown</Text>
            <View style={styles.timingRow}>
              <Text style={styles.timingLabel}>YOLO Inference:</Text>
              <Text style={styles.timingValue}>
                {recognitionResult.yoloTimeMs.toFixed(1)}ms
              </Text>
            </View>
            {recognitionResult.cards.map((card, idx) => (
              <View key={idx} style={styles.timingRow}>
                <Text style={styles.timingLabel}>
                  {' '}
                  Card {idx + 1} Embedding:
                </Text>
                <Text style={styles.timingValue}>
                  {card.embeddingTimeMs.toFixed(1)}ms
                </Text>
              </View>
            ))}
            <View style={[styles.timingRow, styles.timingTotal]}>
              <Text style={styles.timingLabelTotal}>Total Pipeline:</Text>
              <Text style={styles.timingValueTotal}>
                {recognitionResult.totalTimeMs.toFixed(1)}ms
              </Text>
            </View>
          </View>

          <Text style={styles.timing}>
            Found {recognitionResult.cards.length} card
            {recognitionResult.cards.length !== 1 ? 's' : ''}
          </Text>

          {recognitionResult.cards.map((card, cardIndex) => (
            <View key={cardIndex} style={styles.recognizedCard}>
              <Text style={styles.detectedCardTitle}>
                Card #{cardIndex + 1} (Confidence:{' '}
                {(card.conf * 100).toFixed(1)}%)
              </Text>
              <Text style={styles.info}>
                Embedding extracted in {card.embeddingTimeMs.toFixed(1)}ms
              </Text>

              {card.matches.length > 0 && (
                <>
                  <Text style={styles.matchesTitle}>Top Matches:</Text>
                  {card.matches.slice(0, 3).map((match, matchIndex) => (
                    <View key={matchIndex} style={styles.matchResult}>
                      <Text style={styles.matchRank}>#{matchIndex + 1}</Text>
                      <View style={styles.matchInfo}>
                        <Text style={styles.matchName}>{match.name}</Text>
                        <Text style={styles.matchId}>ID: {match.cardId}</Text>
                      </View>
                      <Text style={styles.matchScore}>
                        {(match.score * 100).toFixed(1)}%
                      </Text>
                    </View>
                  ))}
                </>
              )}
              {card.matches.length === 0 && (
                <Text style={styles.info}>⚠️ No matches found in database</Text>
              )}
            </View>
          ))}
        </View>
      )}

      {testResult && (
        <Text style={styles.result}>Test Result: {testResult}</Text>
      )}

      {error && <Text style={styles.error}>{error}</Text>}
      {downloadError && <Text style={styles.error}>{downloadError}</Text>}
    </ScrollView>
  );
}

const styles = StyleSheet.create({
  scrollView: {
    flex: 1,
    backgroundColor: '#f5f5f5',
  },
  container: {
    padding: 20,
    alignItems: 'center',
  },
  title: {
    fontSize: 24,
    fontWeight: 'bold',
    marginBottom: 10,
    marginTop: 40,
  },
  subtitle: {
    fontSize: 16,
    color: '#666',
    marginBottom: 30,
    textAlign: 'center',
  },
  separator: {
    marginVertical: 5,
  },
  result: {
    fontSize: 18,
    marginTop: 15,
    color: '#007AFF',
    fontWeight: '600',
  },
  timing: {
    fontSize: 16,
    marginTop: 8,
    color: '#34C759',
    fontWeight: '500',
  },
  info: {
    fontSize: 14,
    marginTop: 5,
    color: '#666',
  },
  error: {
    fontSize: 16,
    marginTop: 20,
    color: '#FF3B30',
    textAlign: 'center',
  },
  resultBox: {
    marginTop: 20,
    padding: 15,
    backgroundColor: '#fff',
    borderRadius: 10,
    width: '100%',
    shadowColor: '#000',
    shadowOffset: { width: 0, height: 2 },
    shadowOpacity: 0.1,
    shadowRadius: 4,
    elevation: 3,
  },
  sectionTitle: {
    fontSize: 18,
    fontWeight: 'bold',
    marginBottom: 15,
    color: '#333',
  },
  cardResult: {
    flexDirection: 'row',
    alignItems: 'center',
    paddingVertical: 10,
    borderBottomWidth: 1,
    borderBottomColor: '#eee',
  },
  cardRank: {
    fontSize: 16,
    fontWeight: 'bold',
    color: '#666',
    width: 40,
  },
  cardInfo: {
    flex: 1,
    marginLeft: 10,
  },
  cardName: {
    fontSize: 15,
    fontWeight: '600',
    color: '#333',
  },
  cardId: {
    fontSize: 12,
    color: '#999',
    marginTop: 2,
  },
  cardScore: {
    fontSize: 16,
    fontWeight: 'bold',
    color: '#34C759',
    marginLeft: 10,
  },
  imageContainer: {
    marginTop: 20,
    marginBottom: 20,
    alignItems: 'center',
    width: '100%',
  },
  selectedImage: {
    borderRadius: 10,
    marginTop: 10,
    width: 300,
    height: 400,
  },
  detectedCard: {
    marginTop: 15,
    padding: 12,
    backgroundColor: '#f9f9f9',
    borderRadius: 8,
    borderLeftWidth: 4,
    borderLeftColor: '#4CAF50',
  },
  detectedCardTitle: {
    fontSize: 16,
    fontWeight: 'bold',
    color: '#4CAF50',
    marginBottom: 8,
  },
  matchesTitle: {
    fontSize: 14,
    fontWeight: '600',
    color: '#666',
    marginTop: 10,
    marginBottom: 5,
  },
  matchItem: {
    flexDirection: 'row',
    alignItems: 'center',
    paddingVertical: 6,
    borderBottomWidth: 1,
    borderBottomColor: '#eee',
  },
  matchRank: {
    fontSize: 14,
    fontWeight: 'bold',
    color: '#999',
    width: 30,
  },
  dewarpedCard: {
    width: 180,
    height: 252, // 180 / 0.714 ≈ 252
    marginTop: 10,
    marginBottom: 10,
    borderRadius: 8,
    borderWidth: 2,
    borderColor: '#4CAF50',
    alignSelf: 'center',
  },
  recognizedCard: {
    marginTop: 15,
    padding: 12,
    backgroundColor: '#f0f0f0',
    borderRadius: 8,
    borderLeftWidth: 4,
    borderLeftColor: '#9C27B0',
  },
  matchResult: {
    flexDirection: 'row',
    alignItems: 'center',
    paddingVertical: 8,
    borderBottomWidth: 1,
    borderBottomColor: '#ddd',
    marginTop: 4,
  },
  matchInfo: {
    flex: 1,
    marginLeft: 8,
  },
  matchName: {
    fontSize: 14,
    fontWeight: '600',
    color: '#333',
  },
  matchId: {
    fontSize: 11,
    color: '#999',
    marginTop: 2,
  },
  matchScore: {
    fontSize: 15,
    fontWeight: 'bold',
    color: '#9C27B0',
    marginLeft: 8,
  },
  cardGrid: {
    width: '100%',
    flexDirection: 'row',
    flexWrap: 'wrap',
    justifyContent: 'space-around',
    marginTop: 10,
  },
  timingBox: {
    backgroundColor: '#f8f9fa',
    borderRadius: 8,
    padding: 12,
    marginBottom: 15,
    borderLeftWidth: 4,
    borderLeftColor: '#007AFF',
  },
  timingTitle: {
    fontSize: 15,
    fontWeight: '600',
    color: '#333',
    marginBottom: 10,
  },
  timingRow: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    paddingVertical: 4,
  },
  timingLabel: {
    fontSize: 13,
    color: '#666',
  },
  timingValue: {
    fontSize: 13,
    color: '#007AFF',
    fontWeight: '500',
  },
  timingTotal: {
    marginTop: 8,
    paddingTop: 8,
    borderTopWidth: 1,
    borderTopColor: '#ddd',
  },
  timingLabelTotal: {
    fontSize: 14,
    color: '#333',
    fontWeight: '600',
  },
  timingValueTotal: {
    fontSize: 14,
    color: '#34C759',
    fontWeight: 'bold',
  },
  tableHeader: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    paddingVertical: 8,
    borderBottomWidth: 2,
    borderBottomColor: '#ccc',
    marginBottom: 5,
  },
  tableHeaderText: {
    fontWeight: 'bold',
    fontSize: 14,
    color: '#333',
    flex: 1,
  },
  tableRow: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    paddingVertical: 6,
    borderBottomWidth: 1,
    borderBottomColor: '#eee',
  },
  tableCell: {
    fontSize: 13,
    color: '#555',
    flex: 1,
  },
});
