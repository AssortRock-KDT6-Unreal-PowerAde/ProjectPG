// Talks to UnrealMCPython's FMCPythonTcpServer (Plugins/UnrealMCPython/Source/.../MCPythonTcpServer.cpp).
// Protocol: connect, send one JSON object, read until the server closes the socket
// (it always closes after writing exactly one JSON response — see
// FMCPythonTcpServer::ProcessDataOnGameThread), then parse that as the response.

import net from "node:net";

export function callUnreal({
  host,
  port,
  moduleName,
  functionName,
  args,
  connectTimeoutMs,
  responseTimeoutMs,
}) {
  return new Promise((resolve, reject) => {
    const socket = new net.Socket();
    let settled = false;
    let connectTimer = null;
    let responseTimer = null;
    const chunks = [];

    const cleanup = () => {
      clearTimeout(connectTimer);
      clearTimeout(responseTimer);
    };

    const fail = (err) => {
      if (settled) return;
      settled = true;
      cleanup();
      socket.destroy();
      reject(err);
    };

    const succeed = (raw) => {
      if (settled) return;
      settled = true;
      cleanup();
      resolve(raw);
    };

    connectTimer = setTimeout(() => {
      fail(
        new Error(
          `Timed out connecting to Unreal Editor at ${host}:${port} after ${connectTimeoutMs}ms. ` +
            `Is the editor running with the UnrealMCPython plugin loaded?`
        )
      );
    }, connectTimeoutMs);

    socket.once("error", (err) => {
      if (err.code === "ECONNREFUSED") {
        fail(
          new Error(
            `Connection refused at ${host}:${port}. Is the Unreal Editor running with UnrealMCPython loaded?`
          )
        );
      } else {
        fail(err);
      }
    });

    socket.connect(port, host, () => {
      clearTimeout(connectTimer);

      const payload = JSON.stringify({
        type: "python_call",
        module: moduleName,
        function: functionName,
        args: args ?? {},
      });
      socket.write(payload, "utf8");

      responseTimer = setTimeout(() => {
        fail(new Error(`Timed out waiting for response from Unreal Editor after ${responseTimeoutMs}ms.`));
      }, responseTimeoutMs);

      socket.on("data", (chunk) => chunks.push(chunk));
      socket.on("close", () => succeed(Buffer.concat(chunks).toString("utf8")));
    });
  });
}
