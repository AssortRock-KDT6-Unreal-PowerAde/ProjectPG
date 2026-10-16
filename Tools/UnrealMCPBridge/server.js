#!/usr/bin/env node
// MCP stdio bridge for the UnrealMCPython plugin.
//
// UnrealMCPython runs a raw TCP+JSON server inside the Unreal Editor
// (127.0.0.1:12029 by default) that accepts {"type":"python_call","module":...,
// "function":...,"args":{...}} and dispatches to a Python function in
// Content/Python/UnrealMCPython/*_actions.py. This process wraps that protocol
// as a standard MCP server over stdio: every public `ue_*` function across the
// action modules is discovered at startup (see lib/parse-actions.js) and exposed
// as one MCP tool named `<module>__<function>`.

import { Server } from "@modelcontextprotocol/sdk/server/index.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { ListToolsRequestSchema, CallToolRequestSchema } from "@modelcontextprotocol/sdk/types.js";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { discoverTools } from "./lib/parse-actions.js";
import { callUnreal } from "./lib/tcp-client.js";

const __dirname = path.dirname(fileURLToPath(import.meta.url));

const PYTHON_DIR =
  process.env.UE_MCP_PYTHON_DIR ||
  path.resolve(__dirname, "..", "..", "Plugins", "UnrealMCPython", "Content", "Python", "UnrealMCPython");
const HOST = process.env.UE_MCP_HOST || "127.0.0.1";
const PORT = parseInt(process.env.UE_MCP_PORT || "12029", 10);
const CONNECT_TIMEOUT_MS = parseInt(process.env.UE_MCP_CONNECT_TIMEOUT_MS || "5000", 10);
const RESPONSE_TIMEOUT_MS = parseInt(process.env.UE_MCP_RESPONSE_TIMEOUT_MS || "30000", 10);

const tools = discoverTools(PYTHON_DIR);
const toolsByName = new Map(tools.map((t) => [t.toolName, t]));

if (tools.length === 0) {
  console.error(`[unreal-mcpython-bridge] No tools discovered under ${PYTHON_DIR}. Check UE_MCP_PYTHON_DIR.`);
}

const server = new Server(
  { name: "unreal-mcpython-bridge", version: "1.0.0" },
  { capabilities: { tools: {} } }
);

server.setRequestHandler(ListToolsRequestSchema, async () => ({
  tools: tools.map((t) => ({
    name: t.toolName,
    description: t.description,
    inputSchema: t.inputSchema,
  })),
}));

server.setRequestHandler(CallToolRequestSchema, async (request) => {
  const { name, arguments: args } = request.params;
  const tool = toolsByName.get(name);
  if (!tool) {
    return { content: [{ type: "text", text: `Unknown tool: ${name}` }], isError: true };
  }

  let raw;
  try {
    raw = await callUnreal({
      host: HOST,
      port: PORT,
      moduleName: tool.moduleName,
      functionName: tool.functionName,
      args: args || {},
      connectTimeoutMs: CONNECT_TIMEOUT_MS,
      responseTimeoutMs: RESPONSE_TIMEOUT_MS,
    });
  } catch (err) {
    return { content: [{ type: "text", text: err.message }], isError: true };
  }

  // Outer envelope: {"success": <ExecPythonCommandEx ok>, "message": ..., "result": <string>}
  let envelope;
  try {
    envelope = JSON.parse(raw);
  } catch {
    return { content: [{ type: "text", text: `Non-JSON response from Unreal Editor: ${raw}` }], isError: true };
  }

  if (envelope.success !== true) {
    return {
      content: [{ type: "text", text: envelope.message || JSON.stringify(envelope) }],
      isError: true,
    };
  }

  // Inner payload: the JSON string that the target ue_* function itself returned,
  // e.g. {"success": true/false, "message": ..., ...}.
  let innerParsed = null;
  try {
    innerParsed = JSON.parse(envelope.result);
  } catch {
    // Not JSON (shouldn't normally happen — execute_action() validates this) — fall through to raw text.
  }

  if (innerParsed && typeof innerParsed === "object") {
    return {
      content: [{ type: "text", text: JSON.stringify(innerParsed, null, 2) }],
      isError: innerParsed.success === false,
    };
  }

  return { content: [{ type: "text", text: String(envelope.result) }] };
});

const transport = new StdioServerTransport();
await server.connect(transport);
