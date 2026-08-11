/**
 * POST /api/mpesa/callback
 * Called by Safaricom with the result of an STK push (success/failure).
 * Parses the callback and acknowledges it. Payment records are logged;
 * wire a durable store (e.g. Vercel KV) here when you want persistent records.
 */
import { parseCallback } from "../_daraja.js";

export default async function handler(request) {
  let body;
  try {
    body = await request.json();
  } catch {
    body = null;
  }

  const parsed = parseCallback(body);
  if (!parsed) {
    return json(400, { ResultCode: 1, ResultDesc: "Invalid callback" });
  }

  // For now: log the confirmed payment. Add Vercel KV / a DB write here
  // for durable records and to power a status endpoint.
  console.log("MPESA_CALLBACK " + JSON.stringify(parsed));

  // Acknowledge Safaricom.
  return json(200, { ResultCode: 0, ResultDesc: "Success" });
}

function json(status, data) {
  return new Response(JSON.stringify(data), {
    status,
    headers: { "content-type": "application/json" },
  });
}
