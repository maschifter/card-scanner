// babel-preset-expo auto-registers react-native-worklets/plugin when the
// package is installed, so no worklets plugin is listed here explicitly.
module.exports = function (api) {
  api.cache(true);
  return {
    presets: ['babel-preset-expo'],
  };
};
