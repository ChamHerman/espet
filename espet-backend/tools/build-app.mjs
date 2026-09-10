// ESPET dashboard build — esbuild JS API.
// Bundles src/index.tsx (React 19 + antd 6 + lobe-ui) into static/vendor/app
// as ESM with code-splitting, so lobe-editor's heavy lazy paths (mermaid,
// shiki, emoji, collaboration) become on-demand chunks, not main-chunk bloat.
//
// Optional `--stub` mode aliases the big non-essential editor deps to safe
// no-op shims (used if a plain build fails to resolve them in CI).
import { build } from "esbuild";
import { fileURLToPath } from "node:url";
import { createHash } from "node:crypto";
import { readFileSync, rmSync, writeFileSync } from "node:fs";
import path from "node:path";

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, "..");
const outdir = path.join(root, "static", "vendor", "app");
const indexHtml = path.join(root, "static", "index.html");
const stubMode = process.argv.includes("--stub");

// Wipe previous output so stale lazy chunks (and their content hashes) don't
// accumulate across builds and confuse the browser's module cache.
rmSync(outdir, { recursive: true, force: true });

/** Safe no-op ESM shim — resolves any named default import to inert values. */
const stubPlugin = {
  name: "lobe-stubs",
  setup(b) {
    if (!stubMode) return;
    const re = /^(shiki|@shikijs\/.*|mermaid|@splinetool\/runtime|@pierre\/diffs|@emoji-mart\/data|yjs|y-protocols|lib0\/.*)$/;
    b.onResolve({ filter: re }, (args) => ({
      path: args.path,
      namespace: "lobe-stub",
    }));
    b.onLoad({ filter: /.*/, namespace: "lobe-stub" }, () => ({
      contents:
        "const s=new Proxy(function(){return s},{get:()=>s,apply:()=>s,construct:()=>({})});export default s;",
      loader: "js",
    }));
  },
};

const result = await build({
  entryPoints: [path.join(root, "src", "index.tsx")],
  bundle: true,
  format: "esm",
  splitting: true,
  target: "es2022",
  platform: "browser",
  jsx: "automatic",
  minify: true,
  sourcemap: false,
  metafile: true,
  define: { "process.env.NODE_ENV": '"production"' },
  loader: { ".wasm": "empty", ".ttf": "empty", ".data": "empty" },
  plugins: [stubPlugin],
  outdir,
  entryNames: "[dir]/[name]",
  chunkNames: "chunks/[name]-[hash]",
  legalComments: "none",
  logLevel: "info",
});

if (result?.metafile) {
  const outputs = Object.entries(result.metafile.outputs);
  const total = outputs.reduce((n, [, o]) => n + o.bytes, 0);
  const main = outputs.filter(([f]) => !f.includes("chunks/"));
  console.log(
    `\napp bundle: ${(total / 1024 / 1024).toFixed(2)} MB total, ` +
      `${main.length} entry/chunk(s) eager-loaded (${main
        .map(([f, o]) => `${path.basename(f)} ${(o.bytes / 1024).toFixed(0)}KB`)
        .join(", ")}), ` +
      `${outputs.length - main.length} lazy chunk(s)` +
      (stubMode ? " [STUB mode]" : "")
  );
}

// Content-hash cache-bust: rewrite the ?v= query on the app entry (index.js +
// index.css) in static/index.html to a short hash of the entry file. Chunk
// imports inside index.js are already content-hashed by esbuild, so busting the
// entry URL is enough to guarantee the browser refetches the whole graph after
// any rebuild. Idempotent — safe to run every build.
try {
  const entryJs = path.join(outdir, "index.js");
  const entryCss = path.join(outdir, "index.css");
  let digest = "";
  for (const f of [entryJs, entryCss]) {
    try {
      digest += createHash("sha256").update(readFileSync(f)).digest("hex");
    } catch {
      /* css may be absent if no styles imported */
    }
  }
  const v = createHash("sha256").update(digest).digest("hex").slice(0, 10);
  let html = readFileSync(indexHtml, "utf8");
  html = html.replace(
    /(\/static\/vendor\/app\/index\.(?:js|css))\?v=[0-9a-zA-Z]+/g,
    (_m, p) => `${p}?v=${v}`
  );
  writeFileSync(indexHtml, html);
  console.log(`cache-bust: index.html → ?v=${v}`);
} catch (e) {
  console.warn("cache-bust skipped:", e.message);
}
