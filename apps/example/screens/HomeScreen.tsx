import React, { useState, useEffect } from 'react';
import {
  View,
  Text,
  StyleSheet,
  ActivityIndicator,
  TouchableOpacity,
  ScrollView,
  Alert,
} from 'react-native';
import {
  autoLoadEmbeddings,
  checkDatabaseStatus,
  DatabaseStats,
} from '../utils/database';
import { runYoloSegmentation } from 'react-native-card-scanner';
import * as FileSystem from 'expo-file-system';
import { Asset } from 'expo-asset';
import { loadModels } from '../utils/models';

export default function HomeScreen() {
  const [isLoading, setIsLoading] = useState(true);
  const [stats, setStats] = useState<DatabaseStats | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [autoLoadMessage, setAutoLoadMessage] = useState<string | null>(null);
  const [isBenchmarkingYolo, setIsBenchmarkingYolo] = useState(false);
  const [yoloResult, setYoloResult] = useState<string | null>(null);

  useEffect(() => {
    initializeDatabase();
  }, []);

  const initializeDatabase = async () => {
    setIsLoading(true);
    setError(null);
    setAutoLoadMessage(null);

    try {
      const result = await autoLoadEmbeddings();

      if (result.error) {
        // setError(result.error);
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

  const refreshStats = async () => {
    setIsLoading(true);
    try {
      const newStats = await checkDatabaseStatus();
      setStats(newStats);
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
              onPress={refreshStats}
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
});
