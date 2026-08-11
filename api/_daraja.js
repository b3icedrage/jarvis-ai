/**
 * Shared Daraja (Safaricom M-Pesa) helpers.
 * Used by the Vercel serverless functions and the local preview server.
 *
 * Env vars (set in Freebuff Keys / Vercel project settings):
 *   MPESA_CONSUMER_KEY, MPESA_CONSUMER_SECRET, MPESA_PASSKEY,
 *   MPESA_SHORTCODE, MPESA_ENV (sandbox | production), MPESA_CALLBACK_URL
 */
const BASE = {
  sandbox: "https://sandbox.safaricom.co.ke",
  production: "https://api.safaricom.co.ke",
};

let tokenCache = null;

function config() {
  const env = (process.env.MPESA_ENV || "sandbox").toLowerCase();
  return {
    env,
    base: BASE[env] || BASE.sandbox,
    consumerKey: process.env.MPESA_CONSUMER_KEY || "",
    consumerSecret: process.env.MPESA_CONSUMER_SECRET || "",
    passkey: process.env.MPESA_PASSKEY || "",
    shortcode: process.env.MPESA_SHORTCODE || "",
    callbackUrl: process.env.MPESA_CALLBACK_URL || "",
  };
}

export function isConfigured() {
  const c = config();
  return !!(c.consumerKey && c.consumerSecret && c.passkey && c.shortcode);
}

export function missingEnvVars() {
  const c = config();
  const names = {
    consumerKey: "MPESA_CONSUMER_KEY",
    consumerSecret: "MPESA_CONSUMER_SECRET",
    passkey: "MPESA_PASSKEY",
    shortcode: "MPESA_SHORTCODE",
  };
  return Object.entries(names)
    .filter(([key]) => !c[key])
    .map(([, name]) => name);
}

async function getToken() {
  const c = config();
  if (tokenCache && tokenCache.expiresAt > Date.now()) return tokenCache.token;

  const auth = Buffer.from(`${c.consumerKey}:${c.consumerSecret}`).toString("base64");
  const res = await fetch(`${c.base}/oauth/v1/generate?grant_type=client_credentials`, {
    method: "GET",
    headers: { Authorization: `Basic ${auth}` },
  });
  if (!res.ok) {
    throw new Error(`Daraja token request failed (HTTP ${res.status})`);
  }
  const data = await res.json();
  if (!data.access_token) {
    throw new Error(`Daraja token request failed: ${JSON.stringify(data)}`);
  }
  const ttlSeconds = Number(data.expires_in || 3600) - 60;
  tokenCache = {
    token: data.access_token,
    expiresAt: Date.now() + ttlSeconds * 1000,
  };
  return data.access_token;
}

function darajaTimestamp() {
  const d = new Date();
  const p = (n) => String(n).padStart(2, "0");
  return `${d.getFullYear()}${p(d.getMonth() + 1)}${p(d.getDate())}${p(d.getHours())}${p(d.getMinutes())}${p(d.getSeconds())}`;
}

/** Normalize a Kenyan phone number to international 254 format, or null. */
export function normalizePhone(raw) {
  let p = String(raw || "").replace(/[\s\-()]/g, "");
  if (p.startsWith("+")) p = p.slice(1);
  if (/^254\d{9}$/.test(p)) return p;
  if (/^0\d{9}$/.test(p)) return "254" + p.slice(1);
  return null;
}

/**
 * Send a real Lipa Na M-Pesa Online (STK push) request.
 * The money goes to the configured business shortcode — no recipient
 * filtering, no fake prompts. Safaricom sends the actual push.
 */
export async function stkPush({ phone, amount, accountRef = "FEE", description = "School fee payment" }) {
  const c = config();
  const msisdn = normalizePhone(phone);
  if (!msisdn) {
    throw new Error("Invalid phone number — use a Kenyan number like 07XXXXXXXX or +2547XXXXXXXX");
  }
  const amt = Math.round(Number(amount));
  if (!Number.isFinite(amt) || amt < 1 || amt > 150000) {
    throw new Error("Invalid amount");
  }

  const ts = darajaTimestamp();
  const password = Buffer.from(`${c.shortcode}${c.passkey}${ts}`).toString("base64");
  const token = await getToken();

  const res = await fetch(`${c.base}/mpesa/stkpush/v1/processrequest`, {
    method: "POST",
    headers: {
      Authorization: `Bearer ${token}`,
      "content-type": "application/json",
    },
    body: JSON.stringify({
      BusinessShortCode: c.shortcode,
      Password: password,
      Timestamp: ts,
      TransactionType: "CustomerPayBillOnline",
      Amount: amt,
      PartyA: msisdn,
      PartyB: c.shortcode,
      PhoneNumber: msisdn,
      CallBackURL: c.callbackUrl,
      AccountReference: String(accountRef).slice(0, 12),
      TransactionDesc: String(description).slice(0, 13),
    }),
  });

  const data = await res.json().catch(() => ({}));
  return { httpStatus: res.status, ...data };
}

/** Parse a Daraja STK callback into a flat record (or null if invalid). */
export function parseCallback(body) {
  const cb = body?.Body?.stkCallback;
  if (!cb) return null;
  const metadata = {};
  for (const item of cb.CallbackMetadata?.Item || []) {
    metadata[item.Name] = item.Value;
  }
  return {
    merchantRequestId: cb.MerchantRequestID,
    checkoutRequestId: cb.CheckoutRequestID,
    resultCode: cb.ResultCode,
    resultDesc: cb.ResultDesc,
    ...metadata,
  };
}
