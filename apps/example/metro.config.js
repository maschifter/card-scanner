const { getDefaultConfig } = require('expo/metro-config');

const config = getDefaultConfig(__dirname);

// Add .pte as an asset extension
config.resolver.assetExts.push('pte');
config.resolver.assetExts.push('mdb');

module.exports = config;
