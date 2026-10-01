import { join } from 'node:path';
import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
// TIE kernels are tgpu.fn functions whose bodies compile at build time — the
// typegpu unplugin is required or every kernel fails with "Missing metadata".
import typegpuPlugin from 'unplugin-typegpu/vite';
// WebGPU and getUserMedia require a secure context, and http://<lan-ip> is
// not one (only localhost is) — basic-ssl serves dev over self-signed HTTPS
// so other machines on the LAN can run the scanner. Accept the certificate
// warning once per machine.
import basicSsl from '@vitejs/plugin-basic-ssl';
import { benchmarkImagesPlugin } from './tools/benchmarkImagesPlugin.ts';

const page = (name: string) => join(import.meta.dirname, name);

export default defineConfig({
  plugins: [react(), typegpuPlugin(), basicSsl(), benchmarkImagesPlugin()],
  build: {
    rollupOptions: {
      input: {
        index: page('index.html'),
        ...(process.env.BENCH && { bench: page('bench.html') }),
      },
    },
  },
  server: {
    // Listen on all interfaces so other machines can connect via this
    // machine's IP. /api stays proxied to the backend on THIS machine, so
    // remote clients need no configuration.
    host: true,
    proxy: {
      '/api': 'http://localhost:8787',
    },
  },
});
