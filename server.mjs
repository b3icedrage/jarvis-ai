/**
 * Local preview server.
 * Serves the static site and the API endpoints (reusing the exact
 * handlers Vercel deploys) so the preview behaves like production.
 */
import { createServer } from "node:http";
import { readFile } from "node:fs/promises";
import { extname, join, normalize } from "node:path";
import checkUrl from "./api/check-url.js";
import stkPush from "./api/mpesa/stk-push.js";
import mpesaCallback from "./api/mpesa/callback.js";

const PORT = Number(process.env.PORT || 5000);
const ROOT = process.cwd();

const MIME = {
  ".html": "text/html; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".mjs": "text/javascript; charset=utf-8",
  ".css": "text/css; charset=utf-8",
  ".json": "application/json; charset=utf-8",
  ".svg": "image/svg+xml",
  ".png": "image/png",
  ".ico": "image/x-icon",
  ".txt": "text/plain; charset=utf-8",
};

const API_ROUTES = {
  "/api/check-url": checkUrl,
  "/api/mpesa/stk-push": stkPush,
  "/api/mpesa/callback": mpesaCallback,
};

async function readBody(req) {
  let body = "";
  for await (const chunk of req) body += chunk;
  return body;
}

const server = createServer(async (req, res) => {
  try {
    const url = new URL(req.url, "http://localhost");

    const handler = API_ROUTES[url.pathname];
    if (handler) {
      const request = new Request("http://localhost" + url.pathname, {
        method: req.method,
        headers: req.headers,
        body: ["GET", "HEAD"].includes(req.method) ? undefined : await readBody(req),
      });
      const response = await handler(request);
      res.writeHead(response.status, { "content-type": response.headers.get("content-type") || "application/json" });
      res.end(await response.text());
      return;
    }

    const pathname = url.pathname === "/" ? "/index.html" : decodeURIComponent(url.pathname);
    const file = normalize(join(ROOT, pathname));
    if (!file.startsWith(ROOT)) {
      res.writeHead(403, { "content-type": "text/plain" });
      res.end("Forbidden");
      return;
    }

    const data = await readFile(file);
    res.writeHead(200, { "content-type": MIME[extname(file)] || "application/octet-stream" });
    res.end(data);
  } catch (err) {
    if (err.code === "ENOENT") {
      res.writeHead(404, { "content-type": "text/plain" });
      res.end("Not found");
    } else {
      res.writeHead(500, { "content-type": "text/plain" });
      res.end("Server error");
    }
  }
});

server.listen(PORT, "0.0.0.0", () => {
  console.log(`Project Submission Portal preview on 0.0.0.0:${PORT}`);
});
