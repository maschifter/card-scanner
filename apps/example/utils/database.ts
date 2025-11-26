import { Asset } from 'expo-asset';
import {
  getCardCount,
  swapDatabase,
} from 'react-native-card-scanner';

const DEFAULT_GAME = 'lorcana';

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
 * Load database from bundled .mdb asset
 */
export async function loadDatabaseFromAssets(): Promise<boolean> {
  console.log('📦 Loading database from bundled assets...');

  // Load the .mdb database file from assets
  const dbAsset = Asset.fromModule(require('../assets/data.mdb'));
  await dbAsset.downloadAsync();

  if (!dbAsset.localUri) {
    throw new Error('Failed to load database asset');
  }

  console.log('📦 Database asset downloaded to:', dbAsset.localUri);

  // Swap the database
  const success = swapDatabase(dbAsset.localUri, DEFAULT_GAME);

  if (success) {
    console.log('✅ Database swapped successfully');
  } else {
    throw new Error('Failed to swap database');
  }

  return success;
}

/**
 * Auto-load database if empty
 */
export async function autoLoadDatabase(): Promise<{
  loaded: boolean;
  stats: DatabaseStats;
  error?: string;
}> {
  try {
    const stats = await checkDatabaseStatus(DEFAULT_GAME);

    // If already loaded, skip
    if (stats.isLoaded) {
      console.log('✅ Database already loaded with', stats.cardCount, 'cards');
      return { loaded: false, stats };
    }

    // Load database
    console.log('📦 Database empty, auto-loading from assets...');
    await loadDatabaseFromAssets();

    // Get updated stats
    const newStats = await checkDatabaseStatus(DEFAULT_GAME);

    return { loaded: true, stats: newStats };
  } catch (error) {
    const errorMsg = error instanceof Error ? error.message : String(error);
    console.error('❌ Failed to auto-load database:', errorMsg);
    return {
      loaded: false,
      stats: { cardCount: 0, isLoaded: false },
      error: errorMsg,
    };
  }
}
