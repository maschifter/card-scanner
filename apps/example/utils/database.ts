import { Asset } from 'expo-asset';
import { getCardCount, swapDatabase } from 'react-native-card-scanner';

// Game database configurations
const GAME_DATABASES = [
  { name: 'lorcana', asset: require('../assets/lorcana.mdb') },
  { name: 'mtg', asset: require('../assets/mtg.mdb') },
  { name: 'pokemon', asset: require('../assets/pokemon.mdb') },
  { name: 'onepiece', asset: require('../assets/onepiece.mdb') },
  { name: 'riftbound', asset: require('../assets/riftbound.mdb') },
  { name: 'rise', asset: require('../assets/rise.mdb') },
  { name: 'sorcery', asset: require('../assets/sorcery.mdb') },
  { name: 'fab', asset: require('../assets/fab.mdb') },
];

// Set symbol database (for MTG)
const SET_SYMBOL_DATABASE = {
  name: 'set-symbols',
  asset: require('../assets/mtg/mtg-sets.mdb'),
};

export interface DatabaseStats {
  cardCount: number;
  isLoaded: boolean;
}

export interface GameDatabaseStatus {
  gameName: string;
  stats: DatabaseStats;
  error?: string;
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
    console.log(`Database ${gameName} not initialized yet:`, error);
    return {
      cardCount: 0,
      isLoaded: false,
    };
  }
}

/**
 * Load a single database from bundled .mdb asset
 */
export async function loadDatabaseFromAsset(
  gameName: string,
  assetModule: any,
): Promise<boolean> {
  console.log(`📦 Loading ${gameName} database from bundled assets...`);

  // Load the .mdb database file from assets
  const dbAsset = Asset.fromModule(assetModule);
  await dbAsset.downloadAsync();

  if (!dbAsset.localUri) {
    throw new Error(`Failed to load ${gameName} database asset`);
  }

  console.log(`📦 ${gameName} database asset downloaded to:`, dbAsset.localUri);

  // Swap the database (now returns Promise)
  const result = await swapDatabase(gameName, dbAsset.localUri);

  if (result.success) {
    console.log(`✅ ${gameName} database swapped successfully`);
  } else {
    throw new Error(result.error || `Failed to swap ${gameName} database`);
  }

  return result.success;
}

/**
 * Load set symbol database (MTG sets) - same as game databases
 */
export async function loadSetSymbolDatabase(): Promise<void> {
  try {
    console.log('📦 Loading set symbol database...');
    await loadDatabaseFromAsset(
      SET_SYMBOL_DATABASE.name,
      SET_SYMBOL_DATABASE.asset,
    );
    console.log('✅ Set symbol database loaded successfully');
  } catch (error) {
    console.error('❌ Failed to load set symbol database:', error);
    throw error;
  }
}

/**
 * Load all game databases from assets
 */
export async function loadAllDatabases(): Promise<GameDatabaseStatus[]> {
  console.log('📦 Loading all game databases...');

  const results: GameDatabaseStatus[] = [];

  // Load set symbol database first (for MTG)
  try {
    await loadSetSymbolDatabase();
  } catch (error) {
    console.error(
      '❌ Set symbol database failed to load, continuing with games...',
    );
  }

  for (const game of GAME_DATABASES) {
    try {
      // Check if already loaded
      const stats = await checkDatabaseStatus(game.name);

      if (stats.isLoaded) {
        console.log(
          `✅ ${game.name} database already loaded with ${stats.cardCount} cards`,
        );
        results.push({
          gameName: game.name,
          stats,
        });
        continue;
      }

      // Load database
      console.log(`📦 Loading ${game.name} database...`);
      await loadDatabaseFromAsset(game.name, game.asset);

      // Get updated stats
      const newStats = await checkDatabaseStatus(game.name);
      results.push({
        gameName: game.name,
        stats: newStats,
      });

      console.log(
        `✅ ${game.name} loaded successfully with ${newStats.cardCount} cards`,
      );
    } catch (error) {
      const errorMsg = error instanceof Error ? error.message : String(error);
      console.error(`❌ Failed to load ${game.name} database:`, errorMsg);
      results.push({
        gameName: game.name,
        stats: { cardCount: 0, isLoaded: false },
        error: errorMsg,
      });
    }
  }

  // Summary
  const successCount = results.filter((r) => r.stats.isLoaded).length;
  const totalCount = results.length;
  console.log(
    `📊 Database loading complete: ${successCount}/${totalCount} games loaded`,
  );

  return results;
}

/**
 * Auto-load a single database if empty (legacy function for backwards compatibility)
 */
export async function autoLoadDatabase(gameName: string = 'lorcana'): Promise<{
  loaded: boolean;
  stats: DatabaseStats;
  error?: string;
}> {
  try {
    const stats = await checkDatabaseStatus(gameName);

    // If already loaded, skip
    if (stats.isLoaded) {
      console.log(
        `✅ ${gameName} database already loaded with ${stats.cardCount} cards`,
      );
      return { loaded: false, stats };
    }

    // Find the game config
    const gameConfig = GAME_DATABASES.find((g) => g.name === gameName);
    if (!gameConfig) {
      throw new Error(`Game ${gameName} not found in database configurations`);
    }

    // Load database
    console.log(`📦 ${gameName} database empty, auto-loading from assets...`);
    await loadDatabaseFromAsset(gameName, gameConfig.asset);

    // Get updated stats
    const newStats = await checkDatabaseStatus(gameName);

    return { loaded: true, stats: newStats };
  } catch (error) {
    const errorMsg = error instanceof Error ? error.message : String(error);
    console.error(`❌ Failed to auto-load ${gameName} database:`, errorMsg);
    return {
      loaded: false,
      stats: { cardCount: 0, isLoaded: false },
      error: errorMsg,
    };
  }
}
