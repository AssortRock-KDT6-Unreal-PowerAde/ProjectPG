// MCP 서버가 세션에 안 붙었을 때 에디터(UnrealMCPython TCP 12029)를 직접 부르는 한 줄 호출기.
// 사용: node Tools/UnrealMCPBridge/call.mjs <module> <function> '<json args>'
//   예: node Tools/UnrealMCPBridge/call.mjs umg_actions ue_get_widget_blueprint_info '{"asset_path":"/Game/PG/UI/Screens/WBP_PGLobby"}'
// 인자가 길면 '@파일경로' 로 JSON 파일을 넘긴다.
import fs from "node:fs";
import { callUnreal } from "./lib/tcp-client.js";

const [moduleName, functionName, rawArgs = "{}"] = process.argv.slice(2);
const argsText = rawArgs.startsWith("@") ? fs.readFileSync(rawArgs.slice(1), "utf8") : rawArgs;
const raw = await callUnreal({
  host: process.env.UE_MCP_HOST || "127.0.0.1",
  port: parseInt(process.env.UE_MCP_PORT || "12029", 10),
  moduleName,
  functionName,
  args: JSON.parse(argsText),
  connectTimeoutMs: 5000,
  responseTimeoutMs: 60000,
});
let out = raw;
try {
  const envelope = JSON.parse(raw);
  out = envelope.result ?? JSON.stringify(envelope);
} catch {}
process.stdout.write(typeof out === "string" ? out : JSON.stringify(out));
process.stdout.write("\n");
