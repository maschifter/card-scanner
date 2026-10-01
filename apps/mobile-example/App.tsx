import React, { useEffect, useState } from 'react';
import { NavigationContainer } from '@react-navigation/native';
import { createBottomTabNavigator } from '@react-navigation/bottom-tabs';
import { Ionicons } from '@expo/vector-icons';
import { View, Text, ActivityIndicator, StyleSheet } from 'react-native';
import { loadAllDatabases } from './utils/database';
import HomeScreen from './screens/HomeScreen';
import MultipleScannerScreen from './screens/MultipleScannerScreen';
import VisionCameraScanner from './screens/VisionCameraScanner';
import VisionCameraDebug from './screens/VisionCameraDebug';
import BenchmarkScreen from './screens/BenchmarkScreen';

const Tab = createBottomTabNavigator();

export default function App() {
  const [isLoading, setIsLoading] = useState(true);
  const [loadError, setLoadError] = useState<string | null>(null);

  useEffect(() => {
    initializeDatabases();
  }, []);

  const initializeDatabases = async () => {
    const results = await loadAllDatabases();

    // Check if any databases failed to load
    const failedDatabases = results.filter((r) => !r.stats.isLoaded);
    if (failedDatabases.length === results.length) {
      // All databases failed
      setLoadError('Failed to load any game databases. Please check the logs.');
    } else if (failedDatabases.length > 0) {
      // Some databases failed
      console.warn(
        `${failedDatabases.length} database(s) failed to load:`,
        failedDatabases.map((d) => d.gameName).join(', '),
      );
    }

    // Log summary
    const totalCards = results.reduce((sum, r) => sum + r.stats.cardCount, 0);
    const loadedGames = results.filter((r) => r.stats.isLoaded).length;
    console.log(
      `Databases ready: ${loadedGames}/${results.length} games loaded, ${totalCards} total cards`,
    );

    setIsLoading(false);
  };

  if (isLoading) {
    return (
      <View style={styles.loadingContainer}>
        <ActivityIndicator size="large" color="#4CAF50" />
        <Text style={styles.loadingText}>Loading game databases...</Text>
        <Text style={styles.subText}>
          Lorcana, MTG, Pokémon, One Piece, and more
        </Text>
      </View>
    );
  }

  if (loadError) {
    return (
      <View style={styles.loadingContainer}>
        <Text style={styles.errorText}>Failed to load database:</Text>
        <Text style={styles.errorText}>{loadError}</Text>
      </View>
    );
  }

  return (
    <NavigationContainer>
      <Tab.Navigator
        screenOptions={({ route }) => ({
          tabBarIcon: ({ focused, color, size }) => {
            let iconName: keyof typeof Ionicons.glyphMap = 'home';

            if (route.name === 'Home') {
              iconName = focused ? 'home' : 'home-outline';
            } else if (route.name === 'MultipleScanner') {
              iconName = focused ? 'scan' : 'scan-outline';
            } else if (route.name === 'VisionCamera') {
              iconName = focused ? 'videocam' : 'videocam-outline';
            } else if (route.name === 'Debug') {
              iconName = focused ? 'bug' : 'bug-outline';
            } else if (route.name === 'Benchmark') {
              iconName = focused ? 'speedometer' : 'speedometer-outline';
            }

            return <Ionicons name={iconName} size={size} color={color} />;
          },
          tabBarActiveTintColor: '#4CAF50',
          tabBarInactiveTintColor: 'gray',
          headerStyle: {
            backgroundColor: '#4CAF50',
          },
          headerTintColor: '#fff',
          headerTitleStyle: {
            fontWeight: 'bold',
          },
        })}
      >
        <Tab.Screen
          name="Home"
          component={HomeScreen}
          options={{ title: 'Card Scanner' }}
        />
        <Tab.Screen
          name="MultipleScanner"
          component={MultipleScannerScreen}
          options={{ title: 'Multiple Scanner' }}
        />
        <Tab.Screen
          name="VisionCamera"
          component={VisionCameraScanner}
          options={{
            title: 'Vision Camera',
            headerShown: false,
          }}
        />
        <Tab.Screen
          name="Debug"
          component={VisionCameraDebug}
          options={{
            title: 'Debug View',
            headerShown: false,
          }}
        />
        <Tab.Screen
          name="Benchmark"
          component={BenchmarkScreen}
          options={{ title: 'Benchmark' }}
        />
      </Tab.Navigator>
    </NavigationContainer>
  );
}

const styles = StyleSheet.create({
  loadingContainer: {
    flex: 1,
    justifyContent: 'center',
    alignItems: 'center',
    backgroundColor: '#000',
  },
  loadingText: {
    color: '#fff',
    marginTop: 16,
    fontSize: 16,
    fontWeight: 'bold',
  },
  subText: {
    color: '#aaa',
    marginTop: 8,
    fontSize: 14,
    textAlign: 'center',
  },
  errorText: {
    color: '#F44336',
    textAlign: 'center',
    padding: 20,
    fontSize: 16,
  },
});
