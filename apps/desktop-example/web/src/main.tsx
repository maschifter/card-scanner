import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import App from './App';
import Dock from './Dock';

const container = document.getElementById('root');
if (!container) throw new Error('missing #root');

// One bundle serves both surfaces; the module registers the dock with
// ?view=dock. Chosen here rather than inside App so neither component has a
// conditional return sitting above its hooks.
const params = new URLSearchParams(window.location.search);
const port = Number(params.get('port') ?? 27845);
if (!Number.isInteger(port) || port < 1 || port > 65535) {
  throw new Error(`invalid port: ${params.get('port')}`);
}
const isDock = params.get('view') === 'dock';

createRoot(container).render(
  <StrictMode>{isDock ? <Dock port={port} /> : <App port={port} />}</StrictMode>,
);
