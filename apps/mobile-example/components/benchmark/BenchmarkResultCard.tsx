import React from 'react';
import { View, Text, StyleSheet, TouchableOpacity } from 'react-native';
import type { BenchmarkSummary, StageStat } from '../../utils/benchmarkSummary';
import StatTable, { type StatTableRow } from './StatTable';
import { shared } from './styles';

// Change precision only where it is needed (avoid "0.00" for "0.01" step)
const ms = (value: number) =>
  value > 0 && value < 0.01 ? value.toFixed(3) : value.toFixed(2);

const FAILURE_LABELS = [
  ['No detection', 'noDetection'],
  ['Wrong game (db not searched)', 'gameMismatch'],
  ['Below threshold', 'belowThreshold'],
  ['Wrong match', 'wrongMatch'],
] as const;

const failureRows = (summary: BenchmarkSummary): StatTableRow[] => [
  ...FAILURE_LABELS.map(([label, key]) => ({
    label,
    values: [summary[key], summary.cards[key]],
  })),
  {
    label: 'Correct',
    values: [summary.correct, summary.cards.correct],
    isTotal: true,
  },
];

/** Columns shared by the stage table and every extra-model table. */
const STAGE_COLUMNS = ['n', 'mean (ms)', 'max (ms)'];

const stageRow = (stage: StageStat): StatTableRow => ({
  label: stage.label,
  values: [stage.scans, ms(stage.mean), ms(stage.max)],
});

const stageRows = (summary: BenchmarkSummary): StatTableRow[] => [
  ...summary.stages.map(stageRow),
  {
    label: 'total (warm)',
    values: [summary.pipelineScans, ms(summary.meanMs), ''],
    isTotal: true,
  },
  {
    label: 'total (+cold)',
    values: [summary.coldStartScans, ms(summary.meanMsWithColdStart), ''],
  },
];

const gameRows = (summary: BenchmarkSummary): StatTableRow[] =>
  summary.games.map((game) => ({
    label: game.game,
    values: [
      `${game.scans}/${game.totalScans}`,
      `${game.correct}/${game.totalScans}`,
      game.meanMs.toFixed(1),
    ],
  }));

const StatRow = ({
  label,
  value,
}: {
  label: string;
  value: string | number;
}) => (
  <View style={styles.statRow}>
    <Text style={styles.statLabel}>{label}</Text>
    <Text style={styles.statValue}>{value}</Text>
  </View>
);

interface BenchmarkResultCardProps {
  summary: BenchmarkSummary;
  recordCount?: number;
  iterations: number;
  saving: boolean;
  savedPath: string | null;
  saveError: string | null;
  onSave: () => void;
  onReset: () => void;
}

export default function BenchmarkResultCard({
  summary,
  recordCount,
  iterations,
  saving,
  savedPath,
  saveError,
  onSave,
  onReset,
}: BenchmarkResultCardProps) {
  return (
    <View style={shared.card}>
      <Text style={shared.cardTitle}>Done</Text>
      <StatRow label="Records:" value={recordCount ?? 0} />
      <StatRow label="Scored scans:" value={summary.totalScans} />
      <StatRow
        label="Correct:"
        value={`${summary.correct}/${summary.totalScans} (${summary.accuracy.toFixed(1)}%)`}
      />
      <StatRow
        label="Mean total/scan:"
        value={`${summary.meanMs.toFixed(1)} ms`}
      />
      <Text style={shared.hintText}>
        {`${summary.totalCards} cards x ${iterations} iterations. Timings use the ${summary.pipelineScans} scans that ran the full pipeline.`}
      </Text>

      <View style={styles.divider} />
      <Text style={styles.sectionTitle}>Failures</Text>
      <StatTable
        columns={['scans', 'cards']}
        rows={failureRows(summary)}
        wideLabels
      />

      <View style={styles.divider} />
      <Text style={styles.sectionTitle}>
        {`By stage — ${summary.pipelineScans} scans`}
      </Text>
      <StatTable columns={STAGE_COLUMNS} rows={stageRows(summary)} />
      <Text style={shared.hintText}>
        {`"+cold" includes the ${summary.coldStartScans - summary.pipelineScans} warmup scans.`}
      </Text>

      {summary.extraModels.map((model) => (
        <View key={model.label}>
          <View style={styles.divider} />
          <Text style={styles.sectionTitle}>
            {`${model.label} — ran on ${model.ran}/${model.outOf} scans`}
          </Text>
          <StatTable
            columns={STAGE_COLUMNS}
            rows={model.stages.map(stageRow)}
          />
        </View>
      ))}

      <View style={styles.divider} />
      <Text style={styles.sectionTitle}>By game</Text>
      <StatTable
        columns={['scans', 'correct', 'mean (ms)']}
        rows={gameRows(summary)}
      />

      <View style={styles.divider} />
      <Text style={shared.hintText}>
        Full per-record data is in the JSON — use Save to device.
      </Text>

      <View style={styles.actionRow}>
        <TouchableOpacity
          style={[
            shared.secondaryButton,
            styles.actionButton,
            saving && shared.buttonDisabled,
          ]}
          onPress={onSave}
          disabled={saving}
        >
          <Text style={shared.secondaryButtonText}>
            {saving ? 'Saving...' : 'Save to device'}
          </Text>
        </TouchableOpacity>
        <TouchableOpacity
          style={[
            shared.secondaryButton,
            styles.actionButton,
            styles.resetButton,
            saving && shared.buttonDisabled,
          ]}
          onPress={onReset}
          disabled={saving}
        >
          <Text style={shared.secondaryButtonText}>Reset</Text>
        </TouchableOpacity>
      </View>

      {savedPath && <Text style={styles.pathText}>Saved: {savedPath}</Text>}

      {saveError && (
        <Text style={[shared.statusText, shared.statusBad]}>
          Save failed: {saveError}
        </Text>
      )}
    </View>
  );
}

const styles = StyleSheet.create({
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
  sectionTitle: {
    fontSize: 13,
    fontWeight: '700',
    color: '#666',
    textTransform: 'uppercase',
    letterSpacing: 0.5,
    marginTop: 16,
    marginBottom: 6,
  },
  divider: {
    height: 1,
    backgroundColor: '#eee',
    marginTop: 18,
  },
  actionRow: {
    flexDirection: 'row',
    gap: 10,
    marginTop: 12,
  },
  actionButton: {
    flex: 1,
  },
  resetButton: {
    backgroundColor: '#888',
  },
  pathText: {
    fontSize: 12,
    color: '#999',
    marginTop: 8,
  },
});
