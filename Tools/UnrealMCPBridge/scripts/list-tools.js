#!/usr/bin/env node
// Dev helper: prints every MCP tool the bridge would expose, without needing
// the Unreal Editor running. Useful after editing an *_actions.py module to
// sanity-check that new/changed functions parse the way you expect.

import path from "node:path";
import { fileURLToPath } from "node:url";
import { discoverTools } from "../lib/parse-actions.js";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const PYTHON_DIR =
  process.env.UE_MCP_PYTHON_DIR ||
  path.resolve(__dirname, "..", "..", "..", "Plugins", "UnrealMCPython", "Content", "Python", "UnrealMCPython");

const tools = discoverTools(PYTHON_DIR);
console.log(`Discovered ${tools.length} tools from ${PYTHON_DIR}\n`);

const byModule = new Map();
for (const t of tools) {
  if (!byModule.has(t.moduleName)) byModule.set(t.moduleName, []);
  byModule.get(t.moduleName).push(t);
}

for (const [mod, fns] of [...byModule.entries()].sort()) {
  console.log(`${mod} (${fns.length})`);
  for (const f of fns) {
    const params = Object.keys(f.inputSchema.properties).join(", ");
    console.log(`  ${f.toolName}(${params})`);
  }
}
