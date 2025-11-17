import { cacheDirectory, copyAsync } from 'expo-file-system/legacy';
import { Asset } from 'expo-asset';
import { Platform } from 'react-native';

export interface ModelPaths {
  yolo: string;
  embedding: string;
}

/**
 * Load models from bundled assets
 */
export async function loadModels(): Promise<ModelPaths> {
  console.log('Loading models from assets...');

  // Load assets
  const yoloAsset = await Asset.loadAsync(require('../assets/yolo11n-seg.pte'));
  const embeddingAsset = await Asset.loadAsync(require('../assets/embedding_model.pte'));

  let yoloModelPath: string;
  let embeddingModelPath: string;

  if (Platform.OS === 'android') {
    // Android: Use localUri directly
    yoloModelPath = yoloAsset[0].localUri || yoloAsset[0].uri;
    embeddingModelPath = embeddingAsset[0].localUri || embeddingAsset[0].uri;
  } else {
    // iOS: Copy to cache directory
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

  console.log('Models loaded');
  console.log('YOLO:', yoloModelPath);
  console.log('Embedding:', embeddingModelPath);

  return {
    yolo: yoloModelPath,
    embedding: embeddingModelPath,
  };
}
