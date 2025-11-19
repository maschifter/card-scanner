import { useState, useEffect, useCallback } from 'react';
import * as RNFS from 'react-native-fs';
import { swapDatabase, DatabaseInfo } from './index';

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
