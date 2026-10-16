// Parses UnrealMCPython's Python action modules (Content/Python/UnrealMCPython/*_actions.py)
// and turns every public `ue_*` function into an MCP tool definition, without needing
// to hand-maintain a list of the plugin's ~250 functions.
//
// Only functions matching `def ue_*(...)` are exposed — that prefix is this plugin's
// convention for its public API (private helpers use a leading underscore, and the
// dispatcher/test modules are excluded outright).

import fs from "node:fs";
import path from "node:path";

const EXCLUDED_FILES = new Set(["mcp_unreal_actions.py", "test_umg_actions.py"]);

export function discoverModuleFiles(pythonDir) {
  return fs
    .readdirSync(pythonDir)
    .filter((f) => f.endsWith(".py") && !EXCLUDED_FILES.has(f))
    .sort();
}

// Finds the index of the ')' that closes the '(' at openIdx, tracking string
// literals so quoted parens/brackets/braces inside default values don't confuse it.
function findMatchingParen(src, openIdx) {
  let depth = 0;
  let inStr = null;
  for (let i = openIdx; i < src.length; i++) {
    const c = src[i];
    if (inStr) {
      if (c === inStr && src[i - 1] !== "\\") inStr = null;
      continue;
    }
    if (c === '"' || c === "'") {
      inStr = c;
      continue;
    }
    if (c === "(" || c === "[" || c === "{") depth++;
    else if (c === ")" || c === "]" || c === "}") {
      depth--;
      if (depth === 0) return i;
    }
  }
  return -1;
}

// Splits `text` on top-level commas only (ignores commas nested inside
// (), [], {} or string literals).
function splitTopLevel(text, sep = ",") {
  const parts = [];
  let depth = 0;
  let inStr = null;
  let cur = "";
  for (let i = 0; i < text.length; i++) {
    const c = text[i];
    if (inStr) {
      cur += c;
      if (c === inStr && text[i - 1] !== "\\") inStr = null;
      continue;
    }
    if (c === '"' || c === "'") {
      inStr = c;
      cur += c;
      continue;
    }
    if (c === "(" || c === "[" || c === "{") depth++;
    else if (c === ")" || c === "]" || c === "}") depth--;
    if (c === sep && depth === 0) {
      parts.push(cur);
      cur = "";
    } else {
      cur += c;
    }
  }
  if (cur.trim()) parts.push(cur);
  return parts;
}

function topLevelIndexOf(text, ch) {
  let depth = 0;
  let inStr = null;
  for (let i = 0; i < text.length; i++) {
    const c = text[i];
    if (inStr) {
      if (c === inStr && text[i - 1] !== "\\") inStr = null;
      continue;
    }
    if (c === '"' || c === "'") {
      inStr = c;
      continue;
    }
    if (c === "(" || c === "[" || c === "{") depth++;
    else if (c === ")" || c === "]" || c === "}") depth--;
    if (c === ch && depth === 0) return i;
  }
  return -1;
}

// Drops trailing `# comment` text from a signature fragment, outside of string literals,
// so a stray comment (e.g. after a trailing comma) doesn't get parsed as part of the
// next parameter's name.
function stripComments(text) {
  let out = "";
  let inStr = null;
  for (let i = 0; i < text.length; i++) {
    const c = text[i];
    if (inStr) {
      out += c;
      if (c === inStr && text[i - 1] !== "\\") inStr = null;
      continue;
    }
    if (c === '"' || c === "'") {
      inStr = c;
      out += c;
      continue;
    }
    if (c === "#") {
      const nl = text.indexOf("\n", i);
      if (nl === -1) break;
      i = nl - 1;
      continue;
    }
    out += c;
  }
  return out;
}

function stripQuotes(s) {
  const t = s.trim();
  if ((t.startsWith('"') && t.endsWith('"')) || (t.startsWith("'") && t.endsWith("'"))) {
    return t.slice(1, -1);
  }
  return t;
}

// Returns { present, value } — present=false means "no usable literal default"
// (covers `None`, which in this codebase just means "optional", not a real value).
function parseDefaultLiteral(raw) {
  const t = raw.trim();
  if (t === "None") return { present: false };
  if (t === "True") return { present: true, value: true };
  if (t === "False") return { present: true, value: false };
  if (/^-?\d+$/.test(t)) return { present: true, value: parseInt(t, 10) };
  if (/^-?\d+\.\d+$/.test(t)) return { present: true, value: parseFloat(t) };
  if ((t.startsWith('"') && t.endsWith('"')) || (t.startsWith("'") && t.endsWith("'"))) {
    return { present: true, value: stripQuotes(t) };
  }
  if (t === "[]") return { present: true, value: [] };
  if (t === "{}") return { present: true, value: {} };
  return { present: false };
}

// Maps a Python type-hint string (str, int, Optional[str], List[str], Dict[str, Any], ...)
// to a JSON Schema fragment. Unrecognized/unhinted types fall back to an unconstrained
// schema ({}) rather than guessing wrong.
function pyTypeToJsonSchema(typeStrRaw) {
  let t = typeStrRaw.trim();

  const optMatch = /^Optional\[(.+)\]$/s.exec(t);
  if (optMatch) t = optMatch[1].trim();

  const listMatch = /^(?:List|list)\[(.+)\]$/s.exec(t);
  if (listMatch) {
    return { type: "array", items: pyTypeToJsonSchema(listMatch[1]) };
  }
  if (t === "list" || t === "List") return { type: "array", items: {} };

  const dictMatch = /^(?:Dict|dict)\[(.+)\]$/s.exec(t);
  if (dictMatch || t === "dict" || t === "Dict") return { type: "object" };

  switch (t) {
    case "str":
      return { type: "string" };
    case "int":
      return { type: "integer" };
    case "float":
      return { type: "number" };
    case "bool":
      return { type: "boolean" };
    case "Any":
      return {};
    default:
      return {};
  }
}

function parseFunctionAt(src, defMatch) {
  const funcName = defMatch[1];
  const openIdx = defMatch.index + defMatch[0].length - 1; // position of '('
  const closeIdx = findMatchingParen(src, openIdx);
  if (closeIdx === -1) return null;

  const paramsText = stripComments(src.slice(openIdx + 1, closeIdx));

  const afterParen = src.slice(closeIdx + 1);
  const colonRelIdx = afterParen.indexOf(":");
  if (colonRelIdx === -1) return null;
  const bodyStart = closeIdx + 1 + colonRelIdx + 1;

  // Docstring (if the first statement in the function body is a triple-quoted string):
  // first paragraph becomes the tool description, `:param name: text` lines become
  // individual parameter descriptions.
  let description = funcName;
  const paramDocs = {};
  const afterColon = src.slice(bodyStart);
  const docMatch = /^\s*("""|''')([\s\S]*?)\1/.exec(afterColon);
  if (docMatch) {
    const doc = docMatch[2];
    const docLines = doc.split(/\r?\n/).map((l) => l.trim());
    const summaryLines = [];
    for (const line of docLines) {
      if (line.startsWith(":param") || line.startsWith(":return") || line.startsWith(":raises")) break;
      if (line) summaryLines.push(line);
    }
    if (summaryLines.length) description = summaryLines.join(" ");

    const paramRe = /:param\s+([A-Za-z0-9_]+)\s*:\s*([^\n]*)/g;
    let pm;
    while ((pm = paramRe.exec(doc)) !== null) {
      paramDocs[pm[1]] = pm[2].trim();
    }
  }

  const properties = {};
  const required = [];
  for (const rawPart of splitTopLevel(paramsText)) {
    const part = rawPart.trim();
    if (!part || part === "self") continue;

    const eqIdx = topLevelIndexOf(part, "=");
    const namePart = eqIdx >= 0 ? part.slice(0, eqIdx) : part;
    const defaultStr = eqIdx >= 0 ? part.slice(eqIdx + 1) : null;

    const colonIdx = topLevelIndexOf(namePart, ":");
    let name;
    let typeStr = null;
    if (colonIdx >= 0) {
      name = namePart.slice(0, colonIdx).trim();
      typeStr = namePart.slice(colonIdx + 1).trim();
    } else {
      name = namePart.trim();
    }
    if (!name) continue;

    const schema = typeStr ? pyTypeToJsonSchema(typeStr) : {};
    if (paramDocs[name]) schema.description = paramDocs[name];

    if (defaultStr !== null) {
      const lit = parseDefaultLiteral(defaultStr);
      if (lit.present) schema.default = lit.value;
    } else {
      required.push(name);
    }
    properties[name] = schema;
  }

  return {
    functionName: funcName,
    description,
    inputSchema: {
      type: "object",
      properties,
      required,
      additionalProperties: false,
    },
  };
}

export function parseModule(pythonDir, fileName) {
  const filePath = path.join(pythonDir, fileName);
  const src = fs.readFileSync(filePath, "utf8");
  const moduleName = fileName.replace(/\.py$/, "");

  const functions = [];
  const defRe = /def (ue_[A-Za-z0-9_]+)\s*\(/g;
  let m;
  while ((m = defRe.exec(src)) !== null) {
    const parsed = parseFunctionAt(src, m);
    if (!parsed) continue;
    functions.push({
      toolName: `${moduleName}__${parsed.functionName}`,
      moduleName,
      functionName: parsed.functionName,
      description: parsed.description,
      inputSchema: parsed.inputSchema,
    });
  }
  return functions;
}

export function discoverTools(pythonDir) {
  const tools = [];
  for (const file of discoverModuleFiles(pythonDir)) {
    tools.push(...parseModule(pythonDir, file));
  }
  return tools;
}
