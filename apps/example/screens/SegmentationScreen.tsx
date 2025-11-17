import React, { useState } from 'react';
import { View, Text, Button, StyleSheet, ScrollView, Image, ActivityIndicator, TouchableOpacity } from 'react-native';
import { runYoloSegmentation, YoloSegmentationResult } from 'react-native-card-scanner';
import * as ImagePicker from 'expo-image-picker';
import * as ImageManipulator from 'expo-image-manipulator';
import { cacheDirectory } from 'expo-file-system/legacy';
import { loadModels } from '../utils/models';

// Helper to convert HEIC to JPEG
async function ensureJPEG(uri: string): Promise<string> {
  const isHEIC = uri.toLowerCase().endsWith('.heic') ||
                 uri.toLowerCase().endsWith('.heif') ||
                 uri.includes('.heic') ||
                 uri.includes('.heif');

  if (isHEIC) {
    console.log('HEIC image detected, converting to JPEG...');
    const manipResult = await ImageManipulator.manipulateAsync(
      uri,
      [],
      { compress: 1, format: ImageManipulator.SaveFormat.JPEG }
    );
    console.log('Converted to JPEG:', manipResult.uri);
    return manipResult.uri;
  }

  return uri;
}

export default function SegmentationScreen() {
  const [selectedImage, setSelectedImage] = useState<string | null>(null);
  const [yoloResult, setYoloResult] = useState<YoloSegmentationResult | null>(null);
  const [isProcessing, setIsProcessing] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const pickImage = async () => {
    const result = await ImagePicker.launchImageLibraryAsync({
      mediaTypes: ['images'],
      allowsEditing: false,
      quality: 1,
    });

    if (!result.canceled && result.assets[0]) {
      const jpegUri = await ensureJPEG(result.assets[0].uri);
      setSelectedImage(jpegUri);
      setYoloResult(null);
      setError(null);
    }
  };

  const runSegmentation = async () => {
    if (!selectedImage) {
      setError('Please select an image first');
      return;
    }

    setIsProcessing(true);
    setError(null);
    setYoloResult(null);

    try {
      console.log('Loading YOLO model...');
      const models = await loadModels();
      console.log('Model loaded:', models.yolo);

      console.log('Running YOLO segmentation...');
      const result = runYoloSegmentation(
        models.yolo,
        selectedImage,
        0.25, // confidence threshold
        0.7,  // IOU threshold
        cacheDirectory || '' // output directory for visualized results
      );

      console.log('YOLO result:', result);
      setYoloResult(result);
    } catch (err) {
      const errorMsg = err instanceof Error ? err.message : String(err);
      console.error('Segmentation failed:', errorMsg);
      setError(errorMsg);
    } finally {
      setIsProcessing(false);
    }
  };

  return (
    <ScrollView style={styles.container} contentContainerStyle={styles.contentContainer}>
      <View style={styles.header}>
        <Text style={styles.title}>Segmentation Demo</Text>
        <Text style={styles.subtitle}>YOLO11 Card Detection</Text>
      </View>

      <View style={styles.buttonContainer}>
        <TouchableOpacity style={styles.primaryButton} onPress={pickImage}>
          <Text style={styles.primaryButtonText}>Pick Image</Text>
        </TouchableOpacity>

        {selectedImage && (
          <TouchableOpacity
            style={[styles.primaryButton, styles.secondaryButton]}
            onPress={runSegmentation}
            disabled={isProcessing}
          >
            <Text style={styles.primaryButtonText}>
              {isProcessing ? 'Processing...' : 'Run Segmentation'}
            </Text>
          </TouchableOpacity>
        )}
      </View>

      {selectedImage && (
        <View style={styles.imageCard}>
          <Text style={styles.cardTitle}>Selected Image</Text>
          <Image source={{ uri: selectedImage }} style={styles.image} resizeMode="contain" />
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

      {yoloResult && (
        <>
          <View style={styles.resultCard}>
            <Text style={styles.cardTitle}>Results</Text>
            <View style={styles.statRow}>
              <Text style={styles.statLabel}>Cards Detected:</Text>
              <Text style={styles.statValue}>{yoloResult.detections.length}</Text>
            </View>
            <View style={styles.statRow}>
              <Text style={styles.statLabel}>Inference Time:</Text>
              <Text style={styles.statValue}>{yoloResult.inferenceTimeMs.toFixed(1)} ms</Text>
            </View>
          </View>

          <View style={styles.imageCard}>
            <Text style={styles.cardTitle}>Visualized Result</Text>
            <Image
              source={{ uri: yoloResult.visualizedImagePath }}
              style={styles.image}
              resizeMode="contain"
            />
          </View>

          {yoloResult.dewarpedCardPaths.length > 0 && (
            <View style={styles.imageCard}>
              <Text style={styles.cardTitle}>Dewarped Cards</Text>
              {yoloResult.dewarpedCardPaths.map((path, index) => (
                path ? (
                  <View key={index} style={styles.dewarpedCard}>
                    <Text style={styles.dewarpedTitle}>Card {index + 1}</Text>
                    <Image
                      source={{ uri: path }}
                      style={styles.dewarpedImage}
                      resizeMode="contain"
                    />
                  </View>
                ) : (
                  <View key={index} style={styles.dewarpedCard}>
                    <Text style={styles.dewarpedTitle}>Card {index + 1}</Text>
                    <Text style={styles.dewarpedError}>Failed to dewarp</Text>
                  </View>
                )
              ))}
            </View>
          )}

          {yoloResult.detections.length > 0 && (
            <View style={styles.resultCard}>
              <Text style={styles.cardTitle}>Detection Details</Text>
              {yoloResult.detections.map((detection, index) => (
                <View key={index} style={styles.detectionItem}>
                  <Text style={styles.detectionTitle}>Card {index + 1}</Text>
                  <Text style={styles.detectionText}>
                    Confidence: {(detection.box.conf * 100).toFixed(1)}%
                  </Text>
                  <Text style={styles.detectionText}>
                    Box: [{detection.box.x1.toFixed(0)}, {detection.box.y1.toFixed(0)}] →{' '}
                    [{detection.box.x2.toFixed(0)}, {detection.box.y2.toFixed(0)}]
                  </Text>
                  <Text style={styles.detectionText}>
                    Mask Points: {detection.mask.reduce((sum, contour) => sum + contour.length, 0)}
                  </Text>
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
  dewarpedCard: {
    marginBottom: 16,
    borderTopWidth: 1,
    borderTopColor: '#eee',
    paddingTop: 12,
  },
  dewarpedTitle: {
    fontSize: 14,
    fontWeight: '600',
    color: '#666',
    marginBottom: 8,
  },
  dewarpedImage: {
    width: '100%',
    height: 200,
    borderRadius: 8,
  },
  dewarpedError: {
    fontSize: 14,
    color: '#d32f2f',
    fontStyle: 'italic',
  },
  detectionItem: {
    padding: 12,
    backgroundColor: '#f9f9f9',
    borderRadius: 8,
    marginBottom: 12,
  },
  detectionTitle: {
    fontSize: 16,
    fontWeight: '600',
    color: '#333',
    marginBottom: 8,
  },
  detectionText: {
    fontSize: 14,
    color: '#666',
    marginBottom: 4,
  },
});
