import React from 'react';
import { NavigationContainer } from '@react-navigation/native';
import { createBottomTabNavigator } from '@react-navigation/bottom-tabs';
import { Ionicons } from '@expo/vector-icons';
import HomeScreen from './screens/HomeScreen';
import SegmentationScreen from './screens/SegmentationScreen';
import RecognitionScreen from './screens/RecognitionScreen';
import CameraScanner from './screens/CameraScanner';
import DatabaseManagerScreen from './screens/DatabaseScreen';

const Tab = createBottomTabNavigator();

export default function App() {
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
      </Tab.Navigator>
    </NavigationContainer>
  );
}
