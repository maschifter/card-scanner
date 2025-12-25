module.exports = {
  root: true,

  extends: ['@react-native-community', 'prettier'],

  env: {
    'jest/globals': true,
    es2021: true,
  },

  rules: {
    quotes: ['error', 'single', { avoidEscape: true }],
    'no-console': 'off',

    'react/function-component-definition': [
      'error',
      {
        namedComponents: 'arrow-function',
        unnamedComponents: 'arrow-function',
      },
    ],
  },
};
