import React, { useMemo, useState } from 'react';
import {
  View,
  Text,
  StyleSheet,
  ScrollView,
  ActivityIndicator,
  TouchableOpacity,
  Platform,
} from 'react-native';
import { Asset } from 'expo-asset';
import {
  cacheDirectory,
  copyAsync,
  deleteAsync,
  makeDirectoryAsync,
  getInfoAsync,
  writeAsStringAsync,
} from 'expo-file-system/legacy';
import {
  runBenchmarkFromImages,
  type BenchmarkResult,
  type BenchmarkRecord,
} from '@cardnexus/card-scanner';
import { useScannerLoader } from '../hooks/useScannerLoader';
import { BENCHMARK_IMAGES } from '../utils/benchmarkImages';
import { summarize } from '../utils/benchmarkSummary';
import BenchmarkResultCard from '../components/benchmark/BenchmarkResultCard';
import CardIdLookup from '../components/benchmark/CardIdLookup';

const WARMUP_ITERATIONS = 2;
const BENCHMARK_ITERATIONS = 10;

const BENCHMARK_DIR = `${cacheDirectory}benchmark/`;
// Wiped before every run, so a photo left over from an earlier image list can
// never be fed to the scanner. Saved results live one level up and survive it.
const IMAGES_DIR = `${BENCHMARK_DIR}images/`;

const logRecords = (records: BenchmarkRecord[]) => {
  const s = summarize(records);
  console.log(
    `[Benchmark] ${records.length} records (${s.totalScans} scored), ` +
      `mean total ${s.meanMs.toFixed(1)}ms, ` +
      `${s.correct}/${s.totalScans} correct`,
  );
  console.log(JSON.stringify(records, null, 2));
};

export default function BenchmarkScreen() {
  const [isRunning, setIsRunning] = useState(false);
  const [progressText, setProgressText] = useState('');
  const [result, setResult] = useState<BenchmarkResult | null>(null);
  const [errorMsg, setErrorMsg] = useState<string | null>(null);
  const [saving, setSaving] = useState(false);
  const [savedPath, setSavedPath] = useState<string | null>(null);
  const [saveError, setSaveError] = useState<string | null>(null);

  // Data aggregated once per run
  const summary = useMemo(
    () => (result?.records ? summarize(result.records) : null),
    [result],
  );

  const { isLoading, error: loadError } = useScannerLoader();

  const saveResults = async () => {
    const json = result?.recordsJson;
    if (!json) {
      return;
    }
    setSaving(true);
    setSaveError(null);
    try {
      const dirInfo = await getInfoAsync(BENCHMARK_DIR);
      if (!dirInfo.exists) {
        await makeDirectoryAsync(BENCHMARK_DIR, { intermediates: true });
      }

      const timestamp = new Date().toISOString().replace(/[:.]/g, '-');
      const outputJsonPath = `${BENCHMARK_DIR}benchmark_${Platform.OS}_${timestamp}.json`;

      await writeAsStringAsync(outputJsonPath, json);
      setSavedPath(outputJsonPath);
    } catch (err) {
      setSaveError(err instanceof Error ? err.message : String(err));
    } finally {
      setSaving(false);
    }
  };

  const resetResults = () => {
    setResult(null);
    setErrorMsg(null);
    setSavedPath(null);
    setSaveError(null);
  };

  const runBenchmark = async () => {
    setIsRunning(true);
    setResult(null);
    setErrorMsg(null);
    setSavedPath(null);
    setSaveError(null);

    try {
      await deleteAsync(IMAGES_DIR, { idempotent: true });
      await makeDirectoryAsync(IMAGES_DIR, { intermediates: true });

      setProgressText('Preparing images...');

      const images = await Promise.all(
        BENCHMARK_IMAGES.map(async (entry, index) => {
          const asset = Asset.fromModule(entry.module);
          await asset.downloadAsync();

          const localPath = `${IMAGES_DIR}bench_${index}.jpg`;
          await copyAsync({ from: asset.localUri!, to: localPath });

          return {
            imagePath: localPath,
            game: entry.game,
            cardIds: entry.cardIds,
          };
        }),
      );

      setProgressText(
        `Running ${WARMUP_ITERATIONS + BENCHMARK_ITERATIONS} iterations x ${images.length} images...`,
      );

      const benchmarkResult = await runBenchmarkFromImages(
        images,
        WARMUP_ITERATIONS,
        BENCHMARK_ITERATIONS,
      );

      setResult(benchmarkResult);
      if (benchmarkResult.success) {
        if (benchmarkResult.records) {
          logRecords(benchmarkResult.records);
        }
      } else {
        setErrorMsg(benchmarkResult.error || 'Benchmark failed');
      }
    } catch (err) {
      setErrorMsg(err instanceof Error ? err.message : String(err));
    } finally {
      setIsRunning(false);
      setProgressText('');
    }
  };

  return (
    <ScrollView
      style={styles.container}
      contentContainerStyle={styles.contentContainer}
    >
      <View style={styles.header}>
        <Text style={styles.title}>Benchmark</Text>
        <Text style={styles.subtitle}>
          {BENCHMARK_IMAGES.length} images x{' '}
          {WARMUP_ITERATIONS + BENCHMARK_ITERATIONS} iterations (
          {WARMUP_ITERATIONS} warmup)
        </Text>
      </View>

      <CardIdLookup disabled={isLoading} />

      <TouchableOpacity
        style={[
          styles.primaryButton,
          (isRunning || isLoading) && styles.buttonDisabled,
        ]}
        onPress={runBenchmark}
        disabled={isRunning || isLoading}
      >
        <Text style={styles.primaryButtonText}>
          {isRunning
            ? 'Running...'
            : isLoading
              ? 'Loading Models...'
              : 'Start Benchmark'}
        </Text>
      </TouchableOpacity>

      {isRunning && (
        <View style={styles.loadingCard}>
          <ActivityIndicator size="large" color="#4CAF50" />
          <Text style={styles.loadingText}>{progressText}</Text>
        </View>
      )}

      {loadError && (
        <View style={styles.errorCard}>
          <Text style={styles.errorText}>Model load error: {loadError}</Text>
        </View>
      )}

      {errorMsg && (
        <View style={styles.errorCard}>
          <Text style={styles.errorIcon}>⚠️</Text>
          <Text style={styles.errorText}>{errorMsg}</Text>
        </View>
      )}

      {result?.success && summary && (
        <BenchmarkResultCard
          summary={summary}
          recordCount={result.recordCount}
          iterations={BENCHMARK_ITERATIONS}
          saving={saving}
          savedPath={savedPath}
          saveError={saveError}
          onSave={saveResults}
          onReset={resetResults}
        />
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
    fontSize: 14,
    color: '#666',
    textAlign: 'center',
  },
  primaryButton: {
    backgroundColor: '#4CAF50',
    paddingVertical: 16,
    borderRadius: 8,
    alignItems: 'center',
    marginBottom: 20,
  },
  buttonDisabled: {
    opacity: 0.6,
  },
  primaryButtonText: {
    color: '#fff',
    fontSize: 16,
    fontWeight: '600',
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
    textAlign: 'center',
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
});
