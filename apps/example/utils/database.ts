import {
  documentDirectory,
  cacheDirectory,
  writeAsStringAsync,
} from 'expo-file-system/legacy';
import {
  loadCardEmbeddings,
  getCardCount,
  LoadEmbeddingsResult,
} from 'react-native-card-scanner';

// WARNING: Do not use path - specify only game name - CPP handles pathing!
// export const DATABASE_PATH = `${documentDirectory}objectbox-cards`;

// JSON embeddings path in cache
export const EMBEDDINGS_CACHE_PATH = `${cacheDirectory}lorcana_embeddings.json`;

const DEFAULT_GAME = 'lorocana';

export interface DatabaseStats {
  cardCount: number;
  isLoaded: boolean;
}

/**
 * Check if database has embeddings loaded
 */
export async function checkDatabaseStatus(
  gameName: string,
): Promise<DatabaseStats> {
  try {
    const cardCount = getCardCount(gameName);
    return {
      cardCount,
      isLoaded: cardCount > 0,
    };
  } catch (error) {
    console.log('Database not initialized yet:', error);
    return {
      cardCount: 0,
      isLoaded: false,
    };
  }
}

/**
 * Load embeddings from bundled assets into database
 */
export async function loadEmbeddingsFromAssets(): Promise<LoadEmbeddingsResult> {
  console.log('Loading embeddings from bundled assets...');

  // Load embeddings data from bundled JSON
  const embeddingsData = require('../assets/lorcana_embeddings.json');

  // Write to cache directory
  console.log('Writing embeddings to:', EMBEDDINGS_CACHE_PATH);
  await writeAsStringAsync(
    EMBEDDINGS_CACHE_PATH,
    JSON.stringify(embeddingsData),
  );
  console.log('Embeddings written to cache');

  // Load into ObjectBox database
  const loadResult = loadCardEmbeddings(DEFAULT_GAME, EMBEDDINGS_CACHE_PATH);
  console.log('Load result:', loadResult);

  return loadResult;
}

/**
 * Auto-load embeddings if database is empty
 */
export async function autoLoadEmbeddings(): Promise<{
  loaded: boolean;
  stats: DatabaseStats;
  result?: LoadEmbeddingsResult;
  error?: string;
}> {
  try {
    const stats = await checkDatabaseStatus(DEFAULT_GAME);

    // If already loaded, skip
    if (stats.isLoaded) {
      console.log('Database already loaded with', stats.cardCount, 'cards');
      return { loaded: false, stats };
    }

    // Load embeddings
    console.log('Database empty, auto-loading embeddings...');
    const result = await loadEmbeddingsFromAssets();

    // Get updated stats
    const newStats = await checkDatabaseStatus(DEFAULT_GAME);

    return { loaded: true, stats: newStats, result };
  } catch (error) {
    const errorMsg = error instanceof Error ? error.message : String(error);
    console.error('Failed to auto-load embeddings:', errorMsg);
    return {
      loaded: false,
      stats: { cardCount: 0, isLoaded: false },
      error: errorMsg,
    };
  }
}
