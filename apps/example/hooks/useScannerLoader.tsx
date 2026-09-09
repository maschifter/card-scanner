import { useState, useEffect, useCallback } from 'react';
import { Platform } from 'react-native';
import { Asset } from 'expo-asset';
import { cacheDirectory, copyAsync } from 'expo-file-system/legacy';
import { initializeScanner, releaseScanner } from '@cardnexus/card-scanner';

// One model load shared by every screen.
let scannerLoad: Promise<void> | null = null;
let scannerLoaded = false;
let mountedConsumers = 0;

// Helper to prepare local file path from Expo asset
const prepareAsset = async (module: any, fileName: string) => {
  const asset = Asset.fromModule(module);
  await asset.downloadAsync();
  if (!asset.localUri) throw new Error(`Failed to load ${fileName}`);

  const localPath = `${cacheDirectory}${fileName}`;
  await copyAsync({ from: asset.localUri, to: localPath });
  return localPath;
};

const loadScanner = async () => {
  // Parallelize downloads to speed up boot time
  const [yoloPath, embedPath, setYoloPath, setEmbedPath, fabPath] =
    await Promise.all([
      prepareAsset(
        Platform.OS === 'ios'
          ? require('../assets/CardSegmentationModel.coreml.pte')
          : require('../assets/CardSegmentationModel.xnnpack.pte'),
        'seg.pte',
      ),
      prepareAsset(require('../assets/CardRecognitionModel.pte'), 'embed.pte'),
      prepareAsset(
        Platform.OS === 'ios'
          ? require('../assets/mtg/SetSymbolDetectionModel.coreml.pte')
          : require('../assets/mtg/SetSymbolDetectionModel.xnnpack.pte'),
        'set_yolo.pte',
      ),
      prepareAsset(
        require('../assets/mtg/SetSymbolRecognitionModel.pte'),
        'set_embed.pte',
      ),
      prepareAsset(require('../assets/fab/ColorBarModel.pte'), 'fab.pte'),
    ]);

  const result = await initializeScanner({
    segmentationModelPath: yoloPath,
    embeddingModelPath: embedPath,
    scanMode: 'single',
    segmentationThreshold: 0.6,
    iouThreshold: 0.7,
    confidenceThreshold: 0.6,
    minGameConfidence: 1e-5,
    maxMatches: 5,
    searchCandidates: 100,
    captureImage: true,
    blurThreshold: 0,
    lowLightThreshold: 65,
    lowLightGamma: 2.0,
    maxFrameRate: 20,
    gameClassMapping: {
      0: ['fab'],
      1: ['lorcana'],
      2: ['mtg'],
      3: ['onepiece'],
      4: ['pokemon', 'pokemon-japan'], // model class: "pokemon"
      5: ['riftbound'],
      6: ['rise'],
      7: ['sorcery'],
      8: ['chrono-core'],
      9: ['cyberpunk'],
      10: ['dbs-fusion', 'dbs-masters'], // model class: "dbs"
      11: ['eoa'],
      12: ['grand-archive'],
      13: ['gundam'],
      14: ['naruto-mythos'],
      15: ['palworld'],
      16: ['swu'],
    },
    gameSpecificConfig: {
      mtg: {
        // Optional: Could add MTG-specific embedding model here
        // embeddingModelPath: mtgEmbedPath,
        setSymbolDetection: {
          detectionModelPath: setYoloPath,
          embeddingModelPath: setEmbedPath,
          detectionThreshold: 0.3,
          confidenceThreshold: 0.6,
        },
      },
      fab: {
        // Optional: Could add FAB-specific embedding model here
        // embeddingModelPath: fabEmbedPath,
        colorDetection: { modelPath: fabPath },
      },
    },
  });

  if (!result.success) throw new Error(result.error || 'Init failed');
};

// Free the native scanner once no consumer screen is mounted.
const releaseIfUnused = () => {
  if (mountedConsumers === 0 && scannerLoaded) {
    scannerLoad = null;
    scannerLoaded = false;
    releaseScanner();
  }
};

const ensureScannerLoaded = () => {
  scannerLoad ??= loadScanner().then(
    () => {
      scannerLoaded = true;
      releaseIfUnused();
    },
    (err) => {
      scannerLoad = null;
      throw err;
    },
  );
  return scannerLoad;
};

export const useScannerLoader = () => {
  const [isLoading, setIsLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [retryToken, setRetryToken] = useState(0);

  const retry = useCallback(() => setRetryToken((token) => token + 1), []);

  useEffect(() => {
    let cancelled = false;
    const load = async () => {
      try {
        setIsLoading(true);
        setError(null);
        await ensureScannerLoaded();
        if (cancelled) return;
        setIsLoading(false);
      } catch (err) {
        if (cancelled) return;
        setError(err instanceof Error ? err.message : String(err));
        setIsLoading(false);
      }
    };
    load();
    return () => {
      cancelled = true;
    };
  }, [retryToken]);

  useEffect(() => {
    mountedConsumers += 1;
    return () => {
      mountedConsumers -= 1;
      releaseIfUnused();
    };
  }, []);

  return { isLoading, error, retry };
};
