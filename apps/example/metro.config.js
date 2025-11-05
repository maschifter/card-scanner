const { getDefaultConfig } = require('expo/metro-config');

const config = getDefaultConfig(__dirname);

// Add .pte as an asset extension
config.resolver.assetExts.push('pte');

module.exports = config;
