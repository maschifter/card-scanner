import { useState, useEffect, useCallback } from 'react';
import * as FileSystem from 'expo-file-system/legacy';
import { Asset } from 'expo-asset';
import { swapDatabase, DatabaseInfo } from './index';

/**
 * Load a database from a bundled React Native asset
 * @param assetModule - The require() result, e.g., require('./assets/data.mdb')
 * @param gameName - Game identifier (e.g., 'lorcana', 'mtg', 'pokemon')
 * @returns Promise<boolean> - true on success
 */
export const loadDatabaseFromAsset = async (
  assetModule: any,
  gameName: string,
): Promise<boolean> => {
  console.log(`[loadDatabaseFromAsset] Loading database for game: ${gameName}`);

  const asset = Asset.fromModule(assetModule);
  await asset.downloadAsync();

  if (!asset.localUri) {
    throw new Error(`Failed to load database asset for ${gameName}`);
  }

  console.log(
    `[loadDatabaseFromAsset] Asset downloaded to: ${asset.localUri}`,
  );

  const success = swapDatabase(asset.localUri, gameName);

  if (success) {
    console.log(
      `[loadDatabaseFromAsset] Successfully swapped database for ${gameName}`,
    );
  } else {
    throw new Error(`Failed to swap database for ${gameName}`);
  }

  return success;
};

export const downloadDatabase = async (
  url: string,
  dbName: string,
): Promise<string> => {
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
};

// This function now takes dbBasePath as an argument
export const listDatabases = async (): Promise<DatabaseInfo[]> => {
  console.log(`[listDatabases] Calling native listAvailableGames...`);

  try {
    const databases: DatabaseInfo[] = await listAvailableGames();

    console.log(`[listDatabases] Found ${databases.length} databases.`);
    return databases;
  } catch (error) {
    console.error(
      '[listDatabases] Error listing databases from native core:',
      error,
    );
    // If the native call fails (e.g., JSI module not loaded), return empty.
    return [];
  }
};

// useDatabaseManager now accepts dbBasePath as a parameter
export const useDatabaseManager = () => {
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

        const success = swapDatabase(downloadedPath, targetPath);
        if (success) {
          setDownloadSuccess(`Successfully swapped database for ${dbName}`);
          console.log(
            `[downloadAndSwap] Successfully swapped database for ${dbName}`,
          );
          await refreshDatabases();
          return true;
        } else {
          throw new Error(`Failed to swap database for ${dbName}`);
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
};
