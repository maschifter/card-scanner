import React from 'react';
import { StyleSheet } from 'react-native';
import Animated, {
  useAnimatedStyle,
  withSpring,
  withTiming,
  type SharedValue,
} from 'react-native-reanimated';
import type { ViewBox } from '../utils/cameraCoords';

/**
 * Detections arrive at the scan rate, not the frame rate.
 * That means the bounding box may jump around, hence the spring animation.
 */
const TRACKING_SPRING = { duration: 250, dampingRatio: 1 };
const FADE_DURATION = 120;

/**
 * The scanner's tracked bounding box.
 * Uses SharedValue on the UI thread.
 */
export function BoundingBox({ box }: { box: SharedValue<ViewBox | null> }) {
  const animatedStyle = useAnimatedStyle(() => {
    const rect = box.value;
    if (rect == null) {
      return { opacity: withTiming(0, { duration: FADE_DURATION }) };
    }

    return {
      opacity: withTiming(1, { duration: FADE_DURATION }),
      width: withSpring(rect.width, TRACKING_SPRING),
      height: withSpring(rect.height, TRACKING_SPRING),
      transform: [
        { translateX: withSpring(rect.x, TRACKING_SPRING) },
        { translateY: withSpring(rect.y, TRACKING_SPRING) },
      ],
    };
  });

  return <Animated.View style={[styles.boundingBox, animatedStyle]} />;
}

const styles = StyleSheet.create({
  boundingBox: {
    position: 'absolute',
    top: 0,
    left: 0,
    width: 0,
    height: 0,
    opacity: 0,
    borderWidth: 3,
    borderColor: '#00ff00',
    borderRadius: 4,
  },
});
