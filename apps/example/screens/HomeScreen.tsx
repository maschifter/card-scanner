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
} from '../utils/database';
import { useDatabaseManager } from 'react-native-card-scanner';

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

      setAutoLoadMessage(`Stats loaded for: ${gameName}`);
    } catch (err) {
      const errorMsg = err instanceof Error ? err.message : String(err);
      setError(errorMsg);
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
          {databases.length > 0 ? (
            databases.map((dbInfo: DatabaseInfo) => (
              <TouchableOpacity
                key={dbInfo.gameName}
                style={[
                  styles.gameButton,
                  dbInfo.gameName === selectedGame && styles.gameButtonActive,
                ]}
                onPress={() => setSelectedGame(dbInfo.gameName)}
              >
                <Text
                  style={[
                    styles.gameButtonText,
                    dbInfo.gameName === selectedGame &&
                      styles.gameButtonTextActive,
                  ]}
                >
                  {dbInfo.gameName}
                </Text>
              </TouchableOpacity>
            ))
          ) : (
            <Text style={styles.loadingText}>
              No databases found. Go to the Databases tab to download one.
            </Text>
          )}
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
            <TouchableOpacity
              style={styles.retryButton}
              onPress={initializeDatabase}
            >
              <Text style={styles.retryButtonText}>Retry</Text>
            </TouchableOpacity>
          </View>
        ) : stats ? (
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

            {autoLoadMessage && (
              <View style={styles.messageContainer}>
                <Text style={styles.messageText}>{autoLoadMessage}</Text>
              </View>
            )}

            <TouchableOpacity
              style={styles.refreshButton}
              onPress={() => refreshStats(selectedGame)}
            >
              <Text style={styles.refreshButtonText}>🔄 Refresh Stats</Text>
            </TouchableOpacity>
          </View>
        ) : null}
      </View>

      <View style={styles.featuresCard}>
        <Text style={styles.cardTitle}>Features</Text>
        <View style={styles.featuresList}>
          <FeatureItem
            icon="🎯"
            title="Segmentation"
            description="YOLO11 card detection and segmentation"
          />
          <FeatureItem
            icon="🔍"
            title="Recognition"
            description="Full pipeline with embedding search"
          />
          <FeatureItem
            icon="📷"
            title="Live Camera"
            description="Real-time card scanning"
          />
        </View>
      </View>

      <View style={styles.infoCard}>
        <Text style={styles.infoTitle}>How to use</Text>
        <Text style={styles.infoText}>
          Use the bottom tabs to navigate between different features:
        </Text>
        <Text style={styles.infoText}>
          • <Text style={styles.bold}>Segmentation</Text>: Test YOLO card
          detection
        </Text>
        <Text style={styles.infoText}>
          • <Text style={styles.bold}>Recognition</Text>: Full recognition
          pipeline
        </Text>
        <Text style={styles.infoText}>
          • <Text style={styles.bold}>Live Camera</Text>: Scan cards in
          real-time
        </Text>
      </View>
    </ScrollView>
  );
}

interface FeatureItemProps {
  icon: string;
  title: string;
  description: string;
}

function FeatureItem({ icon, title, description }: FeatureItemProps) {
  return (
    <View style={styles.featureItem}>
      <Text style={styles.featureIcon}>{icon}</Text>
      <View style={styles.featureContent}>
        <Text style={styles.featureTitle}>{title}</Text>
        <Text style={styles.featureDescription}>{description}</Text>
      </View>
    </View>
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
  benchmarkCard: {
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
  benchmarkDescription: {
    fontSize: 14,
    color: '#666',
    marginBottom: 16,
  },
  benchmarkButton: {
    backgroundColor: '#FF9800',
    paddingVertical: 14,
    borderRadius: 8,
    alignItems: 'center',
    flexDirection: 'row',
    justifyContent: 'center',
    gap: 8,
  },
  benchmarkButtonDisabled: {
    backgroundColor: '#FFCC80',
  },
  benchmarkButtonText: {
    color: '#fff',
    fontSize: 16,
    fontWeight: '600',
  },
  benchmarkResult: {
    marginTop: 16,
    backgroundColor: '#FFF3E0',
    padding: 16,
    borderRadius: 8,
    borderLeftWidth: 4,
    borderLeftColor: '#FF9800',
  },
  benchmarkResultText: {
    fontSize: 14,
    color: '#E65100',
    lineHeight: 20,
  },

  dbControlCard: {
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
    borderRadius: 5, // Rounded pill shape
    borderWidth: 2,
    borderColor: '#e0e0e0',
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
});
