import React, { useState, useEffect } from 'react';
import {
  View,
  Text,
  StyleSheet,
  ActivityIndicator,
  TouchableOpacity,
  ScrollView,
} from 'react-native';
import {
  autoLoadDatabase,
  checkDatabaseStatus,
  DatabaseStats,
  useDatabaseManager,
  loadDatabaseFromAsset,
  GAME_DATABASES,
} from '../utils/database';
import { DatabaseInfo, deleteDatabase } from '@cardnexus/card-scanner';

const DEFAULT_GAME_NAME = 'lorcana';

export default function HomeScreen() {
  const [isLoading, setIsLoading] = useState(true);
  const [stats, setStats] = useState<DatabaseStats | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [autoLoadMessage, setAutoLoadMessage] = useState<string | null>(null);
  const [selectedGame, setSelectedGame] = useState<string | null>(
    DEFAULT_GAME_NAME,
  );
  const { databases, refreshDatabases } = useDatabaseManager();

  useEffect(() => {
    refreshDatabases()
      .then(() => {})
      .catch((err) => {
        setError(err instanceof Error ? err.message : String(err));
        setIsLoading(false);
      });
  }, []);

  useEffect(() => {
    if (databases.length > 0 && selectedGame === DEFAULT_GAME_NAME) {
      const defaultExists = databases.find(
        (db) => db.gameName === DEFAULT_GAME_NAME,
      );

      if (!defaultExists) {
        setSelectedGame(databases[0].gameName);
      }
    } else if (databases.length > 0 && selectedGame === null) {
      setSelectedGame(databases[0].gameName);
    }
  }, [databases]);

  useEffect(() => {
    if (selectedGame) {
      refreshStats(selectedGame);
    }
  }, [selectedGame]);

  const refreshStats = async (gameName: string) => {
    // Takes gameName as argument
    setIsLoading(true);
    setError(null);
    setAutoLoadMessage(null);

    refreshDatabases();
    try {
      const newStats = await checkDatabaseStatus(gameName);
      setStats(newStats);

      if (newStats.isLoaded) {
        setAutoLoadMessage(`Stats loaded for: ${gameName}`);
      } else {
        setAutoLoadMessage(null);
      }
    } catch (err) {
      const errorMsg = err instanceof Error ? err.message : String(err);
      console.error(`[HomeScreen] Failed to check database status:`, err);
      // Don't set error for unloaded databases - just set stats to null
      setStats(null);
    } finally {
      setIsLoading(false);
    }
  };

  const initializeDatabase = async () => {
    setIsLoading(true);
    setError(null);
    setAutoLoadMessage(null);

    try {
      const result = await autoLoadDatabase();

      if (result.error) {
        setError(result.error);
      } else if (result.loaded) {
        setAutoLoadMessage(
          `Database initialized with ${result.stats.cardCount} cards`,
        );
      } else {
        setAutoLoadMessage(
          `Database already loaded with ${result.stats.cardCount} cards`,
        );
      }

      setStats(result.stats);
    } catch (err) {
      const errorMsg = err instanceof Error ? err.message : String(err);
      setError(errorMsg);
    } finally {
      setIsLoading(false);
    }
  };

  const handleDeleteDatabase = async () => {
    if (!selectedGame) return;

    setIsLoading(true);
    setError(null);
    setAutoLoadMessage(null);

    try {
      const result = await deleteDatabase(selectedGame);

      if (result.success) {
        setAutoLoadMessage(`Database "${selectedGame}" deleted successfully`);
        setStats(null);
        await refreshDatabases();
      } else {
        setError(result.error || 'Failed to delete database');
      }
    } catch (err) {
      const errorMsg = err instanceof Error ? err.message : String(err);
      setError(`Delete failed: ${errorMsg}`);
    } finally {
      setIsLoading(false);
    }
  };

  const handleReloadDatabase = async () => {
    if (!selectedGame) return;

    const gameConfig = GAME_DATABASES.find((g) => g.name === selectedGame);
    if (!gameConfig) {
      setError(`No asset found for game: ${selectedGame}`);
      return;
    }

    setIsLoading(true);
    setError(null);
    setAutoLoadMessage(null);

    try {
      await loadDatabaseFromAsset(selectedGame, gameConfig.asset);
      await refreshDatabases();
      await refreshStats(selectedGame);
      setAutoLoadMessage(`Database "${selectedGame}" loaded successfully`);
    } catch (err) {
      const errorMsg = err instanceof Error ? err.message : String(err);
      console.error(`[HomeScreen] Load failed:`, err);
      setError(`Load failed: ${errorMsg}`);
    } finally {
      setIsLoading(false);
    }
  };

  const formatTimestamp = (timestamp: string | undefined): string => {
    return !timestamp || timestamp.length < 11
      ? 'N/A'
      : new Date(
          `${timestamp.substring(0, 4)}-${timestamp.substring(
            4,
            6,
          )}-${timestamp.substring(6, 8)}T${timestamp.substring(
            9,
            11,
          )}:${timestamp.substring(11, 13)}:00`,
        ).toLocaleString();
  };

  return (
    <ScrollView
      style={styles.container}
      contentContainerStyle={styles.contentContainer}
    >
      <View style={styles.header}>
        <Text style={styles.title}>Card Scanner</Text>
        <Text style={styles.subtitle}>React Native Demo App</Text>
      </View>

      <View style={styles.statsCard}>
        <Text style={styles.cardTitle}>Database Status</Text>

        <Text style={styles.cardTitle}>Select Database</Text>
        <View style={styles.gameButtonGrid}>
          {GAME_DATABASES.map((gameConfig) => {
            const isLoaded = databases.some(
              (db) => db.gameName === gameConfig.name,
            );
            const isSelected = gameConfig.name === selectedGame;

            return (
              <TouchableOpacity
                key={gameConfig.name}
                style={[
                  styles.gameButton,
                  !isLoaded && !isSelected && styles.gameButtonUnloaded,
                  isSelected && styles.gameButtonActive,
                ]}
                onPress={() => setSelectedGame(gameConfig.name)}
              >
                <Text
                  style={[
                    styles.gameButtonText,
                    !isLoaded && !isSelected && styles.gameButtonTextUnloaded,
                    isSelected && styles.gameButtonTextActive,
                  ]}
                >
                  {gameConfig.name}
                  {!isLoaded && !isSelected && ' (not loaded)'}
                </Text>
              </TouchableOpacity>
            );
          })}
        </View>

        <TouchableOpacity
          style={[styles.refreshButton]}
          onPress={refreshDatabases}
        >
          <Text style={styles.refreshButtonText}>🔄 Refresh Database List</Text>
        </TouchableOpacity>

        <Text style={styles.sectionSubtitle}>
          Target Game: {selectedGame || 'N/A'}
        </Text>

        {isLoading ? (
          <View style={styles.loadingContainer}>
            <ActivityIndicator size="large" color="#4CAF50" />
            <Text style={styles.loadingText}>Checking database...</Text>
          </View>
        ) : error ? (
          <View style={styles.errorContainer}>
            <Text style={styles.errorIcon}>⚠️</Text>
            <Text style={styles.errorText}>{error}</Text>
          </View>
        ) : stats && stats.isLoaded ? (
          <View style={styles.statsContainer}>
            <View style={styles.statRow}>
              <Text style={styles.statLabel}>Status:</Text>
              <View
                style={[
                  styles.statusBadge,
                  stats.isLoaded && styles.statusBadgeSuccess,
                ]}
              >
                <Text style={styles.statusText}>
                  {stats.isLoaded ? '✓ Loaded' : '✗ Empty'}
                </Text>
              </View>
            </View>

            <View style={styles.statRow}>
              <Text style={styles.statLabel}>Cards in Database:</Text>
              <Text style={styles.statValue}>
                {stats.cardCount.toLocaleString()}
              </Text>
            </View>

            <View style={styles.statRow}>
              <Text style={styles.statLabel}>DB Created:</Text>
              <Text style={styles.statValue}>
                {formatTimestamp(stats.creationTimestamp)}
              </Text>
            </View>

            {autoLoadMessage && (
              <View style={styles.messageContainer}>
                <Text style={styles.messageText}>{autoLoadMessage}</Text>
              </View>
            )}

            <TouchableOpacity
              style={styles.refreshButton}
              onPress={() => refreshStats(selectedGame!)}
            >
              <Text style={styles.refreshButtonText}>🔄 Refresh Stats</Text>
            </TouchableOpacity>

            <TouchableOpacity
              style={styles.reloadButton}
              onPress={handleReloadDatabase}
            >
              <Text style={styles.reloadButtonText}>↻ Reload Database</Text>
            </TouchableOpacity>

            <TouchableOpacity
              style={styles.deleteButton}
              onPress={handleDeleteDatabase}
            >
              <Text style={styles.deleteButtonText}>🗑️ Delete Database</Text>
            </TouchableOpacity>
          </View>
        ) : selectedGame ? (
          <View style={styles.notLoadedContainer}>
            <Text style={styles.notLoadedIcon}>📦</Text>
            <Text style={styles.notLoadedText}>
              Database "{selectedGame}" is not loaded
            </Text>
            <TouchableOpacity
              style={styles.reloadButton}
              onPress={handleReloadDatabase}
            >
              <Text style={styles.reloadButtonText}>↻ Load Database</Text>
            </TouchableOpacity>
          </View>
        ) : null}
      </View>
    </ScrollView>
  );
}

const styles = StyleSheet.create({
  container: {
    flex: 1,
    backgroundColor: '#f5f5f5',
  },
  contentContainer: {
    padding: 20,
  },
  header: {
    alignItems: 'center',
    marginBottom: 30,
    marginTop: 20,
  },
  title: {
    fontSize: 32,
    fontWeight: 'bold',
    color: '#333',
    marginBottom: 8,
  },
  subtitle: {
    fontSize: 16,
    color: '#666',
  },
  statsCard: {
    backgroundColor: '#fff',
    borderRadius: 12,
    padding: 20,
    marginBottom: 20,
    shadowColor: '#000',
    shadowOffset: { width: 0, height: 2 },
    shadowOpacity: 0.1,
    shadowRadius: 4,
    elevation: 3,
  },
  cardTitle: {
    fontSize: 20,
    fontWeight: 'bold',
    color: '#333',
    marginBottom: 16,
  },
  loadingContainer: {
    alignItems: 'center',
    paddingVertical: 20,
  },
  loadingText: {
    marginTop: 12,
    fontSize: 14,
    color: '#666',
  },
  errorContainer: {
    alignItems: 'center',
    paddingVertical: 20,
  },
  errorIcon: {
    fontSize: 40,
    marginBottom: 12,
  },
  errorText: {
    fontSize: 14,
    color: '#d32f2f',
    textAlign: 'center',
    marginBottom: 16,
  },
  retryButton: {
    backgroundColor: '#4CAF50',
    paddingHorizontal: 24,
    paddingVertical: 12,
    borderRadius: 8,
  },
  retryButtonText: {
    color: '#fff',
    fontSize: 16,
    fontWeight: '600',
  },
  statsContainer: {
    gap: 12,
  },
  statRow: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
    paddingVertical: 8,
  },
  statLabel: {
    fontSize: 16,
    color: '#666',
  },
  statValue: {
    fontSize: 18,
    fontWeight: 'bold',
    color: '#333',
  },
  statusBadge: {
    paddingHorizontal: 12,
    paddingVertical: 6,
    borderRadius: 6,
    backgroundColor: '#f44336',
  },
  statusBadgeSuccess: {
    backgroundColor: '#4CAF50',
  },
  statusText: {
    color: '#fff',
    fontSize: 14,
    fontWeight: '600',
  },
  messageContainer: {
    backgroundColor: '#e8f5e9',
    padding: 12,
    borderRadius: 8,
    marginTop: 8,
  },
  messageText: {
    color: '#2e7d32',
    fontSize: 14,
  },
  refreshButton: {
    backgroundColor: '#2196F3',
    paddingVertical: 12,
    borderRadius: 8,
    alignItems: 'center',
    marginTop: 12,
  },
  refreshButtonText: {
    color: '#fff',
    fontSize: 16,
    fontWeight: '600',
  },
  featuresCard: {
    backgroundColor: '#fff',
    borderRadius: 12,
    padding: 20,
    marginBottom: 20,
    shadowColor: '#000',
    shadowOffset: { width: 0, height: 2 },
    shadowOpacity: 0.1,
    shadowRadius: 4,
    elevation: 3,
  },
  featuresList: {
    gap: 16,
  },
  featureItem: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 16,
  },
  featureIcon: {
    fontSize: 32,
  },
  featureContent: {
    flex: 1,
  },
  featureTitle: {
    fontSize: 16,
    fontWeight: '600',
    color: '#333',
    marginBottom: 4,
  },
  featureDescription: {
    fontSize: 14,
    color: '#666',
  },
  infoCard: {
    backgroundColor: '#fff',
    borderRadius: 12,
    padding: 20,
    marginBottom: 20,
    shadowColor: '#000',
    shadowOffset: { width: 0, height: 2 },
    shadowOpacity: 0.1,
    shadowRadius: 4,
    elevation: 3,
  },
  infoTitle: {
    fontSize: 18,
    fontWeight: 'bold',
    color: '#333',
    marginBottom: 12,
  },
  infoText: {
    fontSize: 14,
    color: '#666',
    marginBottom: 8,
    lineHeight: 20,
  },
  bold: {
    fontWeight: '600',
    color: '#333',
  },
  sectionSubtitle: {
    fontSize: 16,
    fontWeight: '600',
    color: '#4CAF50',
    marginTop: 16,
    marginBottom: 8,
  },
  gameButtonGrid: {
    flexDirection: 'row',
    flexWrap: 'wrap',
    gap: 10,
    marginTop: 10,
    marginBottom: 15,
  },
  gameButton: {
    backgroundColor: '#f0f0f0',
    paddingHorizontal: 12,
    paddingVertical: 8,
    borderRadius: 5,
    borderWidth: 2,
    borderColor: '#e0e0e0',
  },
  gameButtonUnloaded: {
    backgroundColor: '#fafafa',
    borderColor: '#ccc',
    opacity: 0.6,
  },
  gameButtonActive: {
    backgroundColor: '#4CAF50',
    borderColor: '#306932',
  },
  gameButtonText: {
    color: '#333',
    fontWeight: '600',
    fontSize: 14,
  },
  gameButtonTextUnloaded: {
    color: '#999',
    fontStyle: 'italic',
  },
  gameButtonTextActive: {
    color: '#fff',
    fontWeight: '700',
  },
  refreshButton: {
    backgroundColor: '#607D8B',
    paddingVertical: 10,
    borderRadius: 8,
    alignItems: 'center',
    marginTop: 10,
  },
  refreshButtonText: {
    color: '#fff',
    fontSize: 14,
    fontWeight: '600',
  },
  refreshButtonDisabled: {
    backgroundColor: '#B0BEC5',
  },
  reloadButton: {
    backgroundColor: '#FF9800',
    paddingVertical: 12,
    borderRadius: 8,
    alignItems: 'center',
    marginTop: 12,
  },
  reloadButtonText: {
    color: '#fff',
    fontSize: 16,
    fontWeight: '600',
  },
  deleteButton: {
    backgroundColor: '#f44336',
    paddingVertical: 12,
    borderRadius: 8,
    alignItems: 'center',
    marginTop: 12,
  },
  deleteButtonText: {
    color: '#fff',
    fontSize: 16,
    fontWeight: '600',
  },
  notLoadedContainer: {
    alignItems: 'center',
    paddingVertical: 30,
  },
  notLoadedIcon: {
    fontSize: 48,
    marginBottom: 10,
  },
  notLoadedText: {
    fontSize: 16,
    color: '#666',
    marginBottom: 20,
    textAlign: 'center',
  },
});
