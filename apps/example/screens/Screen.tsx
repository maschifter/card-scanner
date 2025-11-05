import React, { useState } from 'react';
import { View, Text, Button, StyleSheet } from 'react-native';
import { multiply } from 'react-native-card-scanner';
import * as FileSystem from 'expo-file-system';

export default function MainScreen() {
  const [result, setResult] = useState<number | null>(null);

  const handleRunModel = () => {
    // Hardcoded path to model.pte in Documents
    const modelPath = FileSystem.documentDirectory + 'model.pte';
    console.log('Using model path:', modelPath);

    try {
      const value = multiply(modelPath);
      setResult(value);
      console.log('Model result:', value);
    } catch (error) {
      console.error('Error running model:', error);
    }
  };

  return (
    <View style={styles.container}>
      <Text style={styles.title}>Card Scanner Example</Text>
      <Text style={styles.subtitle}>Run MobileNet model inference</Text>

      <Button title="Run Model" onPress={handleRunModel} />

      {result !== null && (
        <Text style={styles.result}>Output tensor size: {result}</Text>
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
});
