import React, { useEffect, useState } from 'react';
import { NavigationContainer } from '@react-navigation/native';
import { createBottomTabNavigator } from '@react-navigation/bottom-tabs';
import { Ionicons } from '@expo/vector-icons';
import { View, Text, ActivityIndicator, StyleSheet } from 'react-native';
import { autoLoadEmbeddings } from './utils/database';
import HomeScreen from './screens/HomeScreen';
import SegmentationScreen from './screens/SegmentationScreen';
import RecognitionScreen from './screens/RecognitionScreen';
import CameraScanner from './screens/CameraScanner';
import DatabaseManagerScreen from './screens/DatabaseScreen';
import VisionCameraScanner from './screens/VisionCameraScanner';

const Tab = createBottomTabNavigator();

export default function App() {
  const [isLoading, setIsLoading] = useState(true);
  const [loadError, setLoadError] = useState<string | null>(null);

  useEffect(() => {
    initializeDatabase();
  }, []);

  const initializeDatabase = async () => {
    console.log('🚀 App starting - initializing database...');
    const result = await autoLoadEmbeddings();

    if (result.error) {
      console.error('❌ Failed to initialize database:', result.error);
      setLoadError(result.error);
    } else {
      console.log(`✅ Database ready with ${result.stats.cardCount} cards`);
    }

    setIsLoading(false);
  };

  if (isLoading) {
    return (
      <View style={styles.loadingContainer}>
        <ActivityIndicator size="large" color="#4CAF50" />
        <Text style={styles.loadingText}>Loading card database...</Text>
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
            } else if (route.name === 'Segmentation') {
              iconName = focused ? 'scan' : 'scan-outline';
            } else if (route.name === 'Recognition') {
              iconName = focused ? 'search' : 'search-outline';
            } else if (route.name === 'Camera') {
              iconName = focused ? 'camera' : 'camera-outline';
            } else if (route.name === 'Databases') {
              iconName = focused ? 'server' : 'server-outline';
            } else if (route.name === 'VisionCamera') {
              iconName = focused ? 'videocam' : 'videocam-outline';
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
          name="Segmentation"
          component={SegmentationScreen}
          options={{ title: 'Segmentation' }}
        />
        <Tab.Screen
          name="Recognition"
          component={RecognitionScreen}
          options={{ title: 'Recognition' }}
        />
        <Tab.Screen
          name="Camera"
          component={CameraScanner}
          options={{ title: 'Live Camera' }}
        />
        <Tab.Screen
          name="Databases"
          component={DatabaseManagerScreen}
          options={{ title: 'Databases' }}
        />
        <Tab.Screen
          name="VisionCamera"
          component={VisionCameraScanner}
          options={{ title: 'Vision Camera' }}
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
  },
  errorText: {
    color: '#F44336',
    textAlign: 'center',
    padding: 20,
    fontSize: 16,
  },
});
