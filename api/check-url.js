/**
 * URL checker — a real reachability check for submitted project websites.
 * Deployed by Vercel as a serverless function (POST /api/check-url).
 * No payment, no deception: it fetches the URL and reports what actually happens.
 */
export default async function handler(request) {
  if (request.method !== "POST") {
    return json(405, { error: "Method not allowed" });
  }

  let body;
  try {
    body = await request.json();
  } catch {
    body = null;
  }

  const raw = body && typeof body.url === "string" ? body.url.trim() : "";

  let target;
  try {
    target = new URL(raw);
  } catch {
    return json(400, { error: "Invalid URL" });
  }

  if (target.protocol !== "http:" && target.protocol !== "https:") {
    return json(400, { error: "URL must start with http:// or https://" });
  }

  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), 10000);

  try {
    const res = await fetch(target, {
      method: "GET",
      redirect: "follow",
      signal: controller.signal,
      headers: { "user-agent": "ProjectSubmissionPortal/1.0" },
    });
    return json(200, {
      reachable: true,
      status: res.status,
      finalUrl: res.url || target.toString(),
      checkedAt: new Date().toISOString(),
    });
  } catch (err) {
    const timedOut = err.name === "AbortError";
    return json(200, {
      reachable: false,
      error: timedOut ? "Timed out after 10 seconds" : "Could not reach the website",
      checkedAt: new Date().toISOString(),
    });
  } finally {
    clearTimeout(timer);
  }
}

function json(status, data) {
  return new Response(JSON.stringify(data), {
    status,
    headers: { "content-type": "application/json" },
  });
}
