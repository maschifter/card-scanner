import { useState, useEffect, useCallback } from 'react';
import { Asset } from 'expo-asset';
import * as FileSystem from 'expo-file-system/legacy';
import {
  getDatabaseInfo,
  swapDatabase,
  listDatabases,
  type DatabaseInfo,
} from '@cardnexus/card-scanner';

// Game database configurations
export const GAME_DATABASES = [
  { name: 'lorcana', asset: require('../assets/lorcana.mdb') },
  { name: 'mtg', asset: require('../assets/mtg.mdb') },
  { name: 'pokemon', asset: require('../assets/pokemon.mdb') },
  { name: 'pokemon-japan', asset: require('../assets/pokemon-japan.mdb') },
  { name: 'onepiece', asset: require('../assets/onepiece.mdb') },
  { name: 'riftbound', asset: require('../assets/riftbound.mdb') },
  { name: 'rise', asset: require('../assets/rise.mdb') },
  { name: 'sorcery', asset: require('../assets/sorcery.mdb') },
  { name: 'fab', asset: require('../assets/fab.mdb') },
  { name: 'cyberpunk', asset: require('../assets/cyberpunk.mdb') },
  { name: 'dbs-masters', asset: require('../assets/dbs-masters.mdb') },
  { name: 'dbs-fusion', asset: require('../assets/dbs-fusion.mdb') },
  { name: 'eoa', asset: require('../assets/eoa.mdb') },
  { name: 'grand-archive', asset: require('../assets/grand-archive.mdb') },
  { name: 'gundam', asset: require('../assets/gundam.mdb') },
  { name: 'naruto-mythos', asset: require('../assets/naruto-mythos.mdb') },
  { name: 'swu', asset: require('../assets/swu.mdb') },
  { name: 'chrono-core', asset: require('../assets/chrono-core.mdb') },
  { name: 'palworld', asset: require('../assets/palworld.mdb') },
];

// Set symbol database (for MTG)
const SET_SYMBOL_DATABASE = {
  name: 'set-symbols',
  asset: require('../assets/mtg/mtg-sets.mdb'),
};

// Bump whenever the bundled .mdb files change. An installed database is
// otherwise kept forever, so a card-recognition model swap would leave old
// embeddings in place and every similarity score would collapse.
const DB_BUNDLE_VERSION = '0.0.11';
const DB_VERSION_MARKER = `${FileSystem.documentDirectory}db-bundle-version.txt`;

async function bundledDatabasesChanged(): Promise<boolean> {
  try {
    const info = await FileSystem.getInfoAsync(DB_VERSION_MARKER);
    if (!info.exists) return true;
    const seen = await FileSystem.readAsStringAsync(DB_VERSION_MARKER);
    return seen.trim() !== DB_BUNDLE_VERSION;
  } catch {
    return true;
  }
}

export interface DatabaseStats {
  cardCount: number;
  isLoaded: boolean;
  creationTimestamp?: string;
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
    const info = await getDatabaseInfo(gameName);
    const cardCount = info?.cardCount ?? 0;
    const creationTimestamp = info?.creationTimestamp ?? '';
    return {
      cardCount,
      isLoaded: cardCount > 0,
      creationTimestamp,
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
  console.log(`Loading ${gameName} database from bundled assets...`);

  // Load the .mdb database file from assets
  const dbAsset = Asset.fromModule(assetModule);
  await dbAsset.downloadAsync();

  if (!dbAsset.localUri) {
    throw new Error(`Failed to load ${gameName} database asset`);
  }

  console.log(`${gameName} database asset downloaded to:`, dbAsset.localUri);

  // Swap the database (now returns Promise)
  console.log(`Swapping ${gameName} database from ${dbAsset.localUri}`);
  const result = await swapDatabase(gameName, dbAsset.localUri);

  if (result.success) {
    console.log(`${gameName} database swapped successfully`);
  } else {
    console.error(`Swap failed for ${gameName}:`, result.error);
    throw new Error(result.error || `Failed to swap ${gameName} database`);
  }

  return result.success;
}

/**
 * Load set symbol database (MTG sets) - same as game databases
 */
export async function loadSetSymbolDatabase(): Promise<void> {
  try {
    console.log('Loading set symbol database...');
    await loadDatabaseFromAsset(
      SET_SYMBOL_DATABASE.name,
      SET_SYMBOL_DATABASE.asset,
    );
    console.log('Set symbol database loaded successfully');
  } catch (error) {
    console.error('Failed to load set symbol database:', error);
    throw error;
  }
}

/**
 * Load all game databases from assets
 */
export async function loadAllDatabases(): Promise<GameDatabaseStatus[]> {
  console.log('Loading all game databases...');

  const results: GameDatabaseStatus[] = [];
  const forceReload = await bundledDatabasesChanged();
  if (forceReload) {
    console.log(
      `Bundled databases changed (${DB_BUNDLE_VERSION}); replacing installed ones`,
    );
  }

  // Load set symbol database first (for MTG)
  try {
    await loadSetSymbolDatabase();
  } catch (error) {
    console.error(
      'Set symbol database failed to load, continuing with games...',
    );
  }

  for (const game of GAME_DATABASES) {
    try {
      // Check if already loaded
      const stats = await checkDatabaseStatus(game.name);

      if (stats.isLoaded && !forceReload) {
        console.log(
          `${game.name} database already loaded with ${stats.cardCount} cards`,
        );
        results.push({
          gameName: game.name,
          stats,
        });
        continue;
      }

      // Load database
      console.log(`Loading ${game.name} database...`);
      await loadDatabaseFromAsset(game.name, game.asset);

      // Get updated stats
      const newStats = await checkDatabaseStatus(game.name);
      results.push({
        gameName: game.name,
        stats: newStats,
      });

      console.log(
        `${game.name} loaded successfully with ${newStats.cardCount} cards`,
      );
    } catch (error) {
      const errorMsg = error instanceof Error ? error.message : String(error);
      console.error(`Failed to load ${game.name} database:`, errorMsg);
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
    `Database loading complete: ${successCount}/${totalCount} games loaded`,
  );

  if (forceReload) {
    try {
      await FileSystem.writeAsStringAsync(DB_VERSION_MARKER, DB_BUNDLE_VERSION);
    } catch (error) {
      console.error('Failed to record database bundle version:', error);
    }
  }

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
        `${gameName} database already loaded with ${stats.cardCount} cards`,
      );
      return { loaded: false, stats };
    }

    // Find the game config
    const gameConfig = GAME_DATABASES.find((g) => g.name === gameName);
    if (!gameConfig) {
      throw new Error(`Game ${gameName} not found in database configurations`);
    }

    // Load database
    console.log(`${gameName} database empty, auto-loading from assets...`);
    await loadDatabaseFromAsset(gameName, gameConfig.asset);

    // Get updated stats
    const newStats = await checkDatabaseStatus(gameName);

    return { loaded: true, stats: newStats };
  } catch (error) {
    const errorMsg = error instanceof Error ? error.message : String(error);
    console.error(`Failed to auto-load ${gameName} database:`, errorMsg);
    return {
      loaded: false,
      stats: { cardCount: 0, isLoaded: false },
      error: errorMsg,
    };
  }
}

/**
 * Download database from URL to temporary directory
 */
export async function downloadDatabase(
  url: string,
  dbName: string,
): Promise<string> {
  console.log(`[downloadDatabase] Called with url: ${url}, dbName: ${dbName}`);
  const tmpDir = FileSystem.cacheDirectory + 'db_downloads/';
  console.log(`[downloadDatabase] Temporary directory: ${tmpDir}`);

  const dirInfo = await FileSystem.getInfoAsync(tmpDir);
  if (!dirInfo.exists) {
    await FileSystem.makeDirectoryAsync(tmpDir, { intermediates: true });
  }

  const tmpPath = tmpDir + `${dbName}.mdb`;
  const fileInfo = await FileSystem.getInfoAsync(tmpPath);
  if (fileInfo.exists) {
    console.log('[downloadDatabase] Deleting old temporary file:', tmpPath);
    await FileSystem.deleteAsync(tmpPath);
  }
  console.log(
    `[downloadDatabase] Temporary file path for download: ${tmpPath}`,
  );

  const downloadResult = await FileSystem.downloadAsync(url, tmpPath);

  if (downloadResult.status !== 200) {
    throw new Error(
      `Failed to download database from ${url}. Status code: ${downloadResult.status}`,
    );
  }
  console.log(`[downloadDatabase] Download successful to: ${tmpPath}`);
  return tmpPath;
}

/**
 * React hook for database management in UI
 */
export function useDatabaseManager() {
  const [isDownloading, setIsDownloading] = useState(false);
  const [downloadError, setDownloadError] = useState<string | null>(null);
  const [downloadSuccess, setDownloadSuccess] = useState<string | null>(null);
  const [databases, setDatabases] = useState<DatabaseInfo[]>([]);

  const refreshDatabases = useCallback(async () => {
    console.log(
      `[useDatabaseManager] Refreshing databases using native core...`,
    );
    const dbList = await listDatabases();
    setDatabases(dbList);
  }, []);

  const downloadAndSwap = useCallback(
    async (dbName: string, downloadUrl: string) => {
      setIsDownloading(true);
      setDownloadError(null);
      setDownloadSuccess(null);
      try {
        console.log(
          `[downloadAndSwap] Starting download and swap for dbName: ${dbName}, from URL: ${downloadUrl}`,
        );
        const downloadedPath = await downloadDatabase(downloadUrl, dbName);
        console.log(
          `[downloadAndSwap] Database downloaded to temporary path: ${downloadedPath}`,
        );

        const targetPath = `${dbName}`;
        console.log(
          `[downloadAndSwap] Calling native swapDatabase with sourcePath: ${downloadedPath}, gameName: ${targetPath}`,
        );

        const result = await swapDatabase(downloadedPath, targetPath);
        if (result.success) {
          setDownloadSuccess(`Successfully swapped database for ${dbName}`);
          console.log(
            `[downloadAndSwap] Successfully swapped database for ${dbName}`,
          );
          await refreshDatabases();
          return true;
        } else {
          throw new Error(
            `Failed to swap database for ${dbName}: ${result.error}`,
          );
        }
      } catch (err) {
        const errorMsg = err instanceof Error ? err.message : String(err);
        setDownloadError(`Failed to download or swap database: ${errorMsg}`);
        console.error(
          '[downloadAndSwap] Error downloading or swapping database:',
          errorMsg,
        );
        return false;
      } finally {
        setIsDownloading(false);
      }
    },
    [refreshDatabases],
  );

  useEffect(() => {
    refreshDatabases();
  }, [refreshDatabases]);

  return {
    downloadAndSwap,
    isDownloading,
    downloadError,
    downloadSuccess,
    databases,
    refreshDatabases,
  };
}
