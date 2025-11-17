import { useState, useEffect, useCallback } from 'react';
import * as RNFS from 'react-native-fs';
import { swapDatabase } from './index';

export const downloadDatabase = async (
  url: string,
  dbName: string,
): Promise<string> => {
  console.log(`[downloadDatabase] Called with url: ${url}, dbName: ${dbName}`);
  const tmpDir = RNFS.CachesDirectoryPath + '/db_downloads/';
  console.log(`[downloadDatabase] Temporary directory: ${tmpDir}`);
  await RNFS.mkdir(tmpDir);

  const tmpPath = tmpDir + `${dbName}.mdb`;
  if (await RNFS.exists(tmpPath)) {
    console.log('[downloadDatabase] Deleting old temporary file:', tmpPath);
    await RNFS.unlink(tmpPath);
  }
  console.log(
    `[downloadDatabase] Temporary file path for download: ${tmpPath}`,
  );

  const options = {
    fromUrl: url,
    toFile: tmpPath,
  };

  const downloadResult = await RNFS.downloadFile(options).promise;

  if (downloadResult.statusCode !== 200) {
    throw new Error(
      `Failed to download database from ${url}. Status code: ${downloadResult.statusCode}`,
    );
  }
  console.log(`[downloadDatabase] Download successful to: ${tmpPath}`);
  return tmpPath;
};

export interface DatabaseInfo {
  gameName: string;
  path: string;
}

// This function now takes dbBasePath as an argument
export const listDatabases = async (
  dbBasePath: string,
): Promise<DatabaseInfo[]> => {
  console.log(`[listDatabases] Called with dbBasePath: ${dbBasePath}`);
  if (!dbBasePath) {
    console.log(`[listDatabases] dbBasePath is empty, returning empty array.`);
    return [];
  }
  try {
    // Use RNFS to list directories if possible, otherwise this needs to be passed from native side or consuming app
    // For now, we'll assume RNFS can list directories in the base path
    const items = await RNFS.readDir(dbBasePath);
    const databases: DatabaseInfo[] = [];
    for (const item of items) {
      if (item.isDirectory()) {
        // Only consider directories as potential databases
        console.log(
          `[listDatabases] Found database directory: ${item.name} at path: ${item.path}`,
        );
        databases.push({ gameName: item.name, path: item.path });
      }
    }
    console.log(`[listDatabases] Found ${databases.length} databases.`);
    return databases;
  } catch (error) {
    console.error('[listDatabases] Error listing databases:', error);
    return [];
  }
};

// useDatabaseManager now accepts dbBasePath as a parameter
export const useDatabaseManager = (dbBasePath: string) => {
  const [isDownloading, setIsDownloading] = useState(false);
  const [downloadError, setDownloadError] = useState<string | null>(null);
  const [downloadSuccess, setDownloadSuccess] = useState<string | null>(null);
  const [databases, setDatabases] = useState<DatabaseInfo[]>([]);

  const refreshDatabases = useCallback(async () => {
    console.log(
      `[useDatabaseManager] Refreshing databases for dbBasePath: ${dbBasePath}`,
    );
    const dbList = await listDatabases(dbBasePath);
    setDatabases(dbList);
  }, [dbBasePath]);

  useEffect(() => {
    if (dbBasePath) {
      console.log(
        `[useDatabaseManager] Setting native database base path to: ${dbBasePath}`,
      );
      refreshDatabases();
    } else {
      console.log(
        `[useDatabaseManager] dbBasePath is empty, skipping setDatabaseBasePath and refresh.`,
      );
    }
  }, [dbBasePath, refreshDatabases]);

  const downloadAndSwap = async (dbName: string, downloadUrl: string) => {
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
        `[downloadAndSwap] Calling native swapDatabase with sourcePath: ${downloadedPath}, gameName (targetSuffix): ${targetPath}`,
      );

      const success = swapDatabase(downloadedPath, targetPath);
      if (success) {
        setDownloadSuccess(`Successfully swapped database for ${dbName}`);
        console.log(
          `[downloadAndSwap] Successfully swapped database for ${dbName}`,
        );
        refreshDatabases();
        return true;
      } else {
        throw new new Error(`Failed to swap database for ${dbName}`)();
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
  };

  return {
    downloadAndSwap,
    isDownloading,
    downloadError,
    downloadSuccess,
    databases,
    refreshDatabases,
  };
};
