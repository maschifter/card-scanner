import { defineConfig, type Plugin } from 'vite';
import react from '@vitejs/plugin-react';

/**
 * Inlines the built JS and CSS into index.html so the result is one file, which
 * is what a Browser Source needs. The checks at the end exist because a broken
 * inline bundle still produces valid-looking HTML.
 */
function inlineEverything(): Plugin {
  return {
    name: 'inline-everything',
    enforce: 'post',
    generateBundle(_options, bundle) {
      const html = Object.values(bundle).find(
        (chunk) => chunk.type === 'asset' && chunk.fileName.endsWith('.html'),
      );
      if (!html || html.type !== 'asset') return;

      let source = String(html.source);

      // Matched by shape, not filename: Vite hashes names, and a missed
      // match would delete the chunk while leaving the tag behind.
      for (const [fileName, chunk] of Object.entries(bundle)) {
        if (chunk.type === 'chunk' && fileName.endsWith('.js')) {
          // Vite hoists the tag to <head>. A module script is deferred, an
          // inline classic script is not, and `defer` is ignored on inline
          // scripts - so in <head> it would run before #root exists.
          source = source.replace(
            /<script\b[^>]*\bsrc=["'][^"']+\.js["'][^>]*>\s*<\/script>/,
            '',
          );
          // A literal "</script>" would close the tag early, leaving the
          // rest of the HTML to be parsed as JavaScript.
          const safe = chunk.code
            .replace(/<\/script/gi, '<\\/script')
            .replace(/<!--/g, '<\\!--');

          // A replacer function, never a string: `$&` and friends are
          // substitution patterns and a minified bundle contains them.
          source = source.replace(
            '</body>',
            () => `<script>${safe}</script></body>`,
          );
          delete bundle[fileName];
        } else if (chunk.type === 'asset' && fileName.endsWith('.css')) {
          source = source.replace(
            /<link\b[^>]*\bhref=["'][^"']+\.css["'][^>]*>/,
            () => `<style>${String(chunk.source)}</style>`,
          );
          delete bundle[fileName];
        }
      }

      // Each of these yields a blank overlay that looks fine until it is live.
      if (/\bsrc=["'][^"']+\.js["']/.test(source)) {
        this.error('inline-everything: a script tag survived inlining');
      }
      if (source.indexOf('<script>') < source.indexOf('id="root"')) {
        this.error(
          'inline-everything: inlined script runs before #root exists',
        );
      }

      const start = source.indexOf('<script>') + '<script>'.length;
      const end = source.indexOf('</script', start);
      try {
        // eslint-disable-next-line no-new-func
        new Function(source.slice(start, end));
      } catch (e) {
        this.error(
          `inline-everything: inlined bundle is not valid JavaScript (${
            (e as Error).message
          })`,
        );
      }

      html.source = source;
    },
  };
}

export default defineConfig({
  plugins: [react(), inlineEverything()],
  base: './',
  build: {
    outDir: 'dist',
    // One chunk, nothing to fetch at runtime.
    cssCodeSplit: false,
    assetsInlineLimit: Number.MAX_SAFE_INTEGER,
    rollupOptions: {
      output: { inlineDynamicImports: true, format: 'iife' },
    },
  },
  server: { port: 5174 },
});
