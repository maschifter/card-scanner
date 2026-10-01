import React from 'react';
import { View, Text, StyleSheet } from 'react-native';

export interface StatTableRow {
  label: string;
  values: (string | number)[];
  isTotal?: boolean;
}

interface StatTableProps {
  columns: string[];
  rows: StatTableRow[];
  wideLabels?: boolean;
}

/**
 * Right-aligned numeric table with a leading label column, used for every
 * breakdown on the result card.
 */
export default function StatTable({
  columns,
  rows,
  wideLabels,
}: StatTableProps) {
  const labelStyle = [
    styles.cell,
    wideLabels ? styles.cellWide : styles.cellLeft,
  ];

  return (
    <View>
      <View style={styles.headerRow}>
        <Text style={labelStyle} />
        {columns.map((column) => (
          <Text key={column} style={[styles.cell, styles.headerText]}>
            {column}
          </Text>
        ))}
      </View>
      {rows.map((row) => (
        <View
          key={row.label}
          style={[styles.row, row.isTotal && styles.totalRow]}
        >
          <Text style={labelStyle}>{row.label}</Text>
          {row.values.map((value, index) => (
            <Text key={columns[index] ?? index} style={styles.cell}>
              {value}
            </Text>
          ))}
        </View>
      ))}
    </View>
  );
}

const styles = StyleSheet.create({
  headerRow: {
    flexDirection: 'row',
    borderBottomWidth: 1,
    borderBottomColor: '#eee',
    paddingBottom: 4,
  },
  row: {
    flexDirection: 'row',
    paddingVertical: 3,
  },
  totalRow: {
    borderTopWidth: 1,
    borderTopColor: '#eee',
    marginTop: 3,
    paddingTop: 5,
  },
  cell: {
    flex: 1,
    fontSize: 13,
    color: '#333',
    textAlign: 'right',
    fontVariant: ['tabular-nums'],
  },
  cellLeft: {
    flex: 1.4,
    textAlign: 'left',
    color: '#666',
  },
  cellWide: {
    flex: 5,
    textAlign: 'left',
    color: '#666',
  },
  headerText: {
    fontSize: 11,
    color: '#999',
    fontWeight: '600',
  },
});
