import React, { useState } from 'react';
import {
  View,
  Text,
  StyleSheet,
  ScrollView,
  TouchableOpacity,
  TextInput,
  Modal,
} from 'react-native';
import { doesCardIdExist } from '@cardnexus/card-scanner';
import { GAME_DATABASES } from '../../utils/database';
import { cardName } from '../../utils/cardNames';
import { shared } from './styles';

// If models still loading - cannot lookup.
interface CardIdLookupProps {
  disabled: boolean;
}

export default function CardIdLookup({ disabled }: CardIdLookupProps) {
  const [game, setGame] = useState<string>(
    GAME_DATABASES.find((db) => db.name === 'lorcana')?.name ??
      GAME_DATABASES[0]?.name ??
      '',
  );
  const [pickerOpen, setPickerOpen] = useState(false);
  const [cardId, setCardId] = useState('');
  const [checking, setChecking] = useState(false);
  const [result, setResult] = useState<{
    game: string;
    exists: boolean;
    name?: string;
  } | null>(null);
  const [error, setError] = useState<string | null>(null);

  const canLookup = game.length > 0 && cardId.trim().length > 0;

  const clearResult = () => {
    setResult(null);
    setError(null);
  };

  const runLookup = async () => {
    const trimmed = cardId.trim();
    if (!game || !trimmed) {
      return;
    }
    setChecking(true);
    clearResult();
    try {
      const exists = await doesCardIdExist(game, trimmed);
      setResult({ game, exists, name: cardName(trimmed) });
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    } finally {
      setChecking(false);
    }
  };

  return (
    <View style={shared.card}>
      <Text style={shared.cardTitle}>Verify card_id</Text>
      <Text style={styles.hint}>
        Check whether a card_id you read off a physical card actually exists in
        that game's database.
      </Text>
      <TouchableOpacity
        style={styles.dropdown}
        onPress={() => setPickerOpen(true)}
      >
        <Text style={styles.dropdownText}>{game}</Text>
        <Text style={styles.dropdownCaret}>▾</Text>
      </TouchableOpacity>
      <Modal
        visible={pickerOpen}
        transparent
        animationType="fade"
        onRequestClose={() => setPickerOpen(false)}
      >
        <TouchableOpacity
          style={styles.modalBackdrop}
          activeOpacity={1}
          onPress={() => setPickerOpen(false)}
        >
          <View style={styles.modalSheet}>
            <ScrollView>
              {GAME_DATABASES.map((db) => {
                const selected = db.name === game;
                return (
                  <TouchableOpacity
                    key={db.name}
                    style={[
                      styles.modalOption,
                      selected && styles.modalOptionSelected,
                    ]}
                    onPress={() => {
                      setGame(db.name);
                      clearResult();
                      setPickerOpen(false);
                    }}
                  >
                    <Text
                      style={[
                        styles.modalOptionText,
                        selected && styles.modalOptionTextSelected,
                      ]}
                    >
                      {db.name}
                    </Text>
                  </TouchableOpacity>
                );
              })}
            </ScrollView>
          </View>
        </TouchableOpacity>
      </Modal>
      <TextInput
        style={styles.input}
        placeholder="card_id"
        autoCapitalize="none"
        autoCorrect={false}
        value={cardId}
        onChangeText={(text) => {
          setCardId(text);
          clearResult();
        }}
      />
      <TouchableOpacity
        style={[
          shared.secondaryButton,
          (checking || disabled || !canLookup) && shared.buttonDisabled,
        ]}
        onPress={runLookup}
        disabled={checking || disabled || !canLookup}
      >
        <Text style={shared.secondaryButtonText}>
          {checking ? 'Checking...' : 'Check'}
        </Text>
      </TouchableOpacity>
      {result && (
        <Text
          style={[
            shared.statusText,
            result.exists ? shared.statusOk : shared.statusBad,
          ]}
        >
          {result.exists
            ? `✓ Found in ${result.game}${result.name ? ` — ${result.name}` : ''}`
            : `✗ Not found in ${result.game}`}
        </Text>
      )}
      {error && (
        <Text style={[shared.statusText, shared.statusBad]}>
          Lookup failed: {error}
        </Text>
      )}
    </View>
  );
}

const styles = StyleSheet.create({
  hint: {
    fontSize: 12,
    color: '#999',
    marginBottom: 12,
  },
  dropdown: {
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'space-between',
    borderWidth: 1,
    borderColor: '#ddd',
    borderRadius: 8,
    paddingHorizontal: 12,
    paddingVertical: 10,
    marginBottom: 10,
  },
  dropdownText: {
    fontSize: 14,
    color: '#333',
  },
  dropdownCaret: {
    fontSize: 14,
    color: '#999',
  },
  modalBackdrop: {
    flex: 1,
    backgroundColor: 'rgba(0, 0, 0, 0.4)',
    justifyContent: 'center',
    padding: 40,
  },
  modalSheet: {
    backgroundColor: '#fff',
    borderRadius: 12,
    maxHeight: 300,
    overflow: 'hidden',
  },
  modalOption: {
    paddingHorizontal: 16,
    paddingVertical: 14,
    borderBottomWidth: 1,
    borderBottomColor: '#eee',
  },
  modalOptionSelected: {
    backgroundColor: '#f0f0f0',
  },
  modalOptionText: {
    fontSize: 15,
    color: '#333',
  },
  modalOptionTextSelected: {
    fontWeight: '600',
  },
  input: {
    borderWidth: 1,
    borderColor: '#ddd',
    borderRadius: 8,
    paddingHorizontal: 12,
    paddingVertical: 10,
    fontSize: 14,
    marginBottom: 10,
  },
});
