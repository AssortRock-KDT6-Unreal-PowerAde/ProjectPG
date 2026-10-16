#!/usr/bin/env node
// Dev helper: spins up the bridge as a real MCP stdio server (spawned as a child
// process, exactly like Claude Code would) and drives it through initialize ->
// tools/list -> tools/call. Useful for verifying the bridge end-to-end, with or
// without the Unreal Editor actually running (a call attempt without the editor
// open should fail with a clear "connection refused" message, not hang or crash).
//
// Usage: node scripts/smoke-test.js [toolName] [jsonArgs]
// Example: node scripts/smoke-test.js util_actions__ue_get_project_info {}

import path from "node:path";
import { fileURLToPath } from "node:url";
import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StdioClientTransport } from "@modelcontextprotocol/sdk/client/stdio.js";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const bridgeDir = path.resolve(__dirname, "..");

const toolName = process.argv[2] || "util_actions__ue_get_project_info";
const toolArgs = process.argv[3] ? JSON.parse(process.argv[3]) : {};

const transport = new StdioClientTransport({
  command: process.execPath,
  args: ["server.js"],
  cwd: bridgeDir,
});

const client = new Client({ name: "unreal-mcpython-bridge-smoke-test", version: "1.0.0" }, { capabilities: {} });
await client.connect(transport);

const { tools } = await client.listTools();
console.log(`Discovered ${tools.length} tools via MCP tools/list.`);

if (!tools.find((t) => t.name === toolName)) {
  console.error(`Tool "${toolName}" not found among discovered tools.`);
  await client.close();
  process.exit(1);
}

console.log(`\nCalling ${toolName}(${JSON.stringify(toolArgs)}) ...`);
const result = await client.callTool({ name: toolName, arguments: toolArgs });
console.log("isError:", result.isError ?? false);
for (const block of result.content) {
  if (block.type === "text") console.log(block.text);
}

await client.close();
process.exit(result.isError ? 1 : 0);
