module.exports = {
  dependency: {
    platforms: {
      // C++ sources only - no Java/Kotlin module and no iOS class to register.
      // Declared so RN autolinking emits the pod that mobile-card-scanner
      // depends on.
      android: null,
      ios: {},
    },
  },
};
