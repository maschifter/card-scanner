import React, { useState } from 'react';
import {
  View,
  Text,
  Button,
  StyleSheet,
  ScrollView,
  TextInput,
  Platform,
} from 'react-native';
import * as DocumentPicker from 'expo-document-picker';
import { LoadEmbeddingsResult, DatabaseInfo } from 'react-native-card-scanner';
import { useDatabaseManager } from 'react-native-card-scanner';

const DOWNLOAD_URL_BASE =
  Platform.OS === 'android'
    ? 'http://10.0.2.2:3000/'
    : 'http://localhost:3000/';

export default function DatabaseManagerScreen() {
  const {
    downloadAndSwap,
    isDownloading,
    downloadError,
    downloadSuccess,
    databases,
    refreshDatabases,
  } = useDatabaseManager();

  const [localGameName, setLocalGameName] = useState('lorocana');
  const [downloadUrlSuffix, setDownloadUrlSuffix] = useState('data.mdb');
  const [loadResult, setLoadResult] = useState<LoadEmbeddingsResult | null>(
    null,
  );
  const [error, setError] = useState<string | null>(null);

  const handleDownloadAndSwap = async () => {
    setError(null);
    setLoadResult(null);

    const fullDownloadUrl = DOWNLOAD_URL_BASE + downloadUrlSuffix;

    console.log(
      `Attempting download and swap for ${localGameName} from ${fullDownloadUrl}`,
    );

    const success = await downloadAndSwap(localGameName, fullDownloadUrl);

    if (!success && downloadError) {
      setError(downloadError);
    }
  };

  const handleLoadEmbeddings = async () => {
    setError(null);
    setLoadResult(null);

    try {
      const result = await DocumentPicker.getDocumentAsync({
        type: 'application/json',
        copyToCacheDirectory: true,
      });

      if (result.canceled) {
        return;
      }

      const jsonUri = result.assets[0].uri;
      console.log('Loading embeddings from:', jsonUri);

      const loadResult = loadCardEmbeddings(localGameName, jsonUri);
      setLoadResult(loadResult);
      console.log('✅ Load result:', loadResult);

      await refreshDatabases();
    } catch (err) {
      const errorMsg = err instanceof Error ? err.message : String(err);
      setError(`Failed to load embeddings: ${errorMsg}`);
      console.error('Error loading embeddings:', errorMsg);
    }
  };

  const renderDatabaseList = () => {
    if (databases.length === 0) {
      return (
        <Text style={styles.info}>
          No databases found. Download one to begin.
        </Text>
      );
    }

    return (
      <View style={styles.resultBox}>
        <Text style={styles.sectionTitle}>Available Databases:</Text>
        <View style={styles.tableHeader}>
          <Text style={[styles.tableHeaderText, { flex: 1 }]}>Game Name</Text>
          <Text style={[styles.tableHeaderText, { flex: 3 }]}>
            Path (Native Directory)
          </Text>
        </View>
        {databases.map((db, index) => (
          <View key={index} style={styles.tableRow}>
            <Text style={[styles.tableCell, { flex: 1 }]}>{db.gameName}</Text>
            <Text style={[styles.tableCell, { flex: 3, fontSize: 10 }]}>
              {db.path}
            </Text>
          </View>
        ))}
      </View>
    );
  };

  return (
    <ScrollView
      style={styles.scrollView}
      contentContainerStyle={styles.container}
    >
      <Text style={styles.title}>💾 Database Manager</Text>
      <Text style={styles.subtitle}>
        Manage local storage and database swap operations.
      </Text>

      {/* Input Fields for Operations */}
      <View style={styles.inputGroup}>
        <Text style={styles.inputLabel}>Game Identifier (DB Name):</Text>
        <TextInput
          style={styles.input}
          value={localGameName}
          onChangeText={setLocalGameName}
          placeholder="e.g., mtg, lorocana"
        />
        <Text style={styles.inputLabel}>Download URL Suffix (File Name):</Text>
        <TextInput
          style={styles.input}
          value={downloadUrlSuffix}
          onChangeText={setDownloadUrlSuffix}
          placeholder="e.g., data.mdb"
        />
      </View>

      {/* Swap/Download Section */}
      <View style={styles.buttonGroup}>
        <Button
          title={isDownloading ? 'Downloading...' : 'Download & Swap DB'}
          onPress={handleDownloadAndSwap}
          disabled={isDownloading || !localGameName}
          color="#1976D2"
        />
        <Button
          title="Refresh List"
          onPress={refreshDatabases}
          color="#607D8B"
        />
      </View>

      {/* Load JSON Section */}
      <View style={styles.separator} />
      <Button
        title={`📁 Load Embeddings into ${localGameName}`}
        onPress={handleLoadEmbeddings}
        disabled={!localGameName}
        color="#FF9800"
      />

      {/* Status Messages */}
      {isDownloading && (
        <Text style={styles.info}>Downloading and swapping database...</Text>
      )}
      {downloadSuccess && <Text style={styles.success}>{downloadSuccess}</Text>}
      {error && <Text style={styles.error}>{error}</Text>}
      {downloadError && <Text style={styles.error}>{downloadError}</Text>}

      {/* Load JSON Result */}
      {loadResult && (
        <View style={styles.resultBox}>
          <Text style={styles.success}>Loaded {loadResult.loaded} cards</Text>
          <Text style={styles.info}>
            Total cards in DB: {loadResult.totalCards}
          </Text>
        </View>
      )}

      {/* Database List */}
      <View style={styles.separator} />
      {renderDatabaseList()}
    </ScrollView>
  );
}

const styles = StyleSheet.create({
  scrollView: {
    flex: 1,
    backgroundColor: '#f5f5f5',
  },
  container: {
    padding: 20,
    alignItems: 'center',
  },
  title: {
    fontSize: 28,
    fontWeight: 'bold',
    marginBottom: 10,
    marginTop: 40,
    color: '#1976D2',
  },
  subtitle: {
    fontSize: 16,
    color: '#666',
    marginBottom: 30,
    textAlign: 'center',
  },
  separator: {
    marginVertical: 15,
    height: 1,
    width: '100%',
    backgroundColor: '#ddd',
  },
  resultBox: {
    marginTop: 15,
    padding: 15,
    backgroundColor: '#fff',
    borderRadius: 10,
    width: '100%',
    shadowColor: '#000',
    shadowOffset: { width: 0, height: 2 },
    shadowOpacity: 0.1,
    shadowRadius: 4,
    elevation: 3,
  },
  sectionTitle: {
    fontSize: 18,
    fontWeight: 'bold',
    marginBottom: 15,
    color: '#333',
  },
  info: {
    fontSize: 14,
    marginTop: 5,
    color: '#666',
    textAlign: 'center',
  },
  success: {
    fontSize: 16,
    marginTop: 10,
    color: '#388E3C',
    fontWeight: '600',
    textAlign: 'center',
  },
  error: {
    fontSize: 16,
    marginTop: 10,
    color: '#D32F2F',
    fontWeight: '600',
    textAlign: 'center',
  },
  inputGroup: {
    width: '100%',
    marginBottom: 20,
    padding: 10,
    backgroundColor: '#fff',
    borderRadius: 8,
  },
  inputLabel: {
    fontSize: 14,
    color: '#333',
    marginBottom: 5,
    fontWeight: '500',
  },
  input: {
    height: 40,
    borderColor: '#ccc',
    borderWidth: 1,
    borderRadius: 5,
    paddingHorizontal: 10,
    marginBottom: 10,
    backgroundColor: '#f9f9f9',
  },
  buttonGroup: {
    width: '100%',
    flexDirection: 'row',
    justifyContent: 'space-around',
    marginBottom: 10,
  },
  tableHeader: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    paddingVertical: 8,
    borderBottomWidth: 2,
    borderBottomColor: '#ccc',
    marginBottom: 5,
  },
  tableHeaderText: {
    fontWeight: 'bold',
    fontSize: 14,
    color: '#333',
  },
  tableRow: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    paddingVertical: 6,
    borderBottomWidth: 1,
    borderBottomColor: '#eee',
  },
  tableCell: {
    fontSize: 12,
    color: '#555',
  },
});
