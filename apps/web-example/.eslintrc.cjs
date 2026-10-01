// Standalone: the root config is React Native oriented.
module.exports = {
  root: true,
  parser: '@typescript-eslint/parser',
  plugins: ['@typescript-eslint', 'react-hooks'],
  extends: [
    'eslint:recommended',
    'plugin:@typescript-eslint/recommended',
    'plugin:react-hooks/recommended',
    'prettier',
  ],
  env: { browser: true, es2022: true },
  rules: {
    '@typescript-eslint/no-unused-vars': [
      'error',
      { ignoreRestSiblings: true },
    ],
  },
  overrides: [
    { files: ['server/**/*.mjs'], env: { node: true, browser: false } },
  ],
  ignorePatterns: ['node_modules', 'dist', 'public'],
};
