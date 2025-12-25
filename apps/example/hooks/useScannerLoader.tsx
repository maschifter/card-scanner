import { useState, useEffect } from 'react';
import { Asset } from 'expo-asset';
import { cacheDirectory, copyAsync } from 'expo-file-system/legacy';
import { initializeScanner, releaseScanner } from '@cardnexus/card-scanner';

export const useScannerLoader = (
  scanMode: 'single' | 'multiple' = 'single',
) => {
  const [isLoading, setIsLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    const loadModels = async () => {
      try {
        setIsLoading(true);

        // Helper to prepare local file path from Expo asset
        const prepareAsset = async (module: any, fileName: string) => {
          const asset = Asset.fromModule(module);
          await asset.downloadAsync();
          if (!asset.localUri) throw new Error(`Failed to load ${fileName}`);

          const localPath = `${cacheDirectory}${fileName}`;
          await copyAsync({ from: asset.localUri, to: localPath });
          return localPath;
        };

        // Parallelize downloads to speed up boot time
        const [yoloPath, embedPath, setYoloPath, setEmbedPath, fabPath] =
          await Promise.all([
            prepareAsset(
              require('../assets/segmentation_model.pte'),
              'seg.pte',
            ),
            prepareAsset(require('../assets/embedding_model.pte'), 'embed.pte'),
            prepareAsset(
              require('../assets/mtg/set_symbol_detection.pte'),
              'set_yolo.pte',
            ),
            prepareAsset(
              require('../assets/mtg/set_symbol_embedder.pte'),
              'set_embed.pte',
            ),
            prepareAsset(
              require('../assets/fab/fab_color_classifier.pte'),
              'fab.pte',
            ),
          ]);

        const result = await initializeScanner({
          segmentationModelPath: yoloPath,
          embeddingModelPath: embedPath,
          scanMode,
          segmentationThreshold: 0.7,
          iouThreshold: 0.7,
          confidenceThreshold: 0.6,
          maxMatches: 5,
          searchCandidates: 100,
          captureImage: true,
          blurThreshold: 0,
          lowLightThreshold: 65,
          lowLightGamma: 2.0,
          maxFrameRate: 5,
          gameClassMapping: {
            0: 'fab',
            1: 'lorcana',
            2: 'mtg',
            3: 'onepiece',
            4: 'pokemon',
            5: 'riftbound',
            6: 'rise',
            7: 'sorcery',
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
        setIsLoading(false);
      } catch (err) {
        setError(err instanceof Error ? err.message : String(err));
        setIsLoading(false);
      }
    };

    loadModels();
    return () => {
      releaseScanner();
    };
  }, [scanMode]);

  return { isLoading, error };
};
