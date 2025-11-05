import React, { useState } from 'react';
import { View, Text, Button, StyleSheet, Platform } from 'react-native';
import { runInference, InferenceResult } from 'react-native-card-scanner';
import { cacheDirectory, copyAsync } from 'expo-file-system/legacy';
import { Asset } from 'expo-asset';

export default function MainScreen() {
  const [result, setResult] = useState<InferenceResult | null>(null);
  const [error, setError] = useState<string | null>(null);

  const handleRunModel = async () => {
    setError(null);
    setResult(null);

    try {
      const assetPath = 'mobile_net_v4_small.pte';

      // Load the asset from the bundle
      const [asset] = await Asset.loadAsync(require('../assets/mobile_net_v4_small.pte'));

      let modelPath: string;

      if (Platform.OS === 'android') {
        // For Android, use the downloaded asset path (already in cache)
        modelPath = asset.localUri || asset.uri;
      } else {
        // For iOS, copy to cache directory as native code needs file:// path
        const cachePath = `${cacheDirectory}${assetPath}`;

        // Asset.loadAsync downloads the file, so we can copy it
        console.log('Copying model to cache...');
        await copyAsync({
          from: asset.localUri || asset.uri,
          to: cachePath,
        });

        modelPath = cachePath;
      }

      console.log('Using model path:', modelPath);

      const inferenceResult = runInference(modelPath);
      setResult(inferenceResult);
      console.log('Model output shape:', inferenceResult.outputShape);
      console.log('Inference time:', inferenceResult.inferenceTimeMs, 'ms');
    } catch (err) {
      const errorMsg = err instanceof Error ? err.message : String(err);
      setError(errorMsg);
      console.error('Error running model:', errorMsg);
    }
  };

  return (
    <View style={styles.container}>
      <Text style={styles.title}>Card Scanner Example</Text>
      <Text style={styles.subtitle}>Run MobileNetV4 model inference</Text>

      <Button title="Run Model" onPress={handleRunModel} />

      {result !== null && (
        <>
          <Text style={styles.result}>
            Output shape: [{result.outputShape.join(', ')}]
          </Text>
          <Text style={styles.timing}>
            Inference time: {result.inferenceTimeMs.toFixed(2)} ms
          </Text>
        </>
      )}

      {error && (
        <Text style={styles.error}>Error: {error}</Text>
      )}
    </View>
  );
}

const styles = StyleSheet.create({
  container: {
    flex: 1,
    justifyContent: 'center',
    alignItems: 'center',
    padding: 20,
    backgroundColor: '#f5f5f5',
  },
  title: {
    fontSize: 24,
    fontWeight: 'bold',
    marginBottom: 10,
  },
  subtitle: {
    fontSize: 16,
    color: '#666',
    marginBottom: 30,
  },
  result: {
    fontSize: 20,
    marginTop: 20,
    color: '#007AFF',
    fontWeight: '600',
  },
  timing: {
    fontSize: 18,
    marginTop: 10,
    color: '#34C759',
    fontWeight: '500',
  },
  error: {
    fontSize: 16,
    marginTop: 20,
    color: '#FF3B30',
    textAlign: 'center',
  },
});
