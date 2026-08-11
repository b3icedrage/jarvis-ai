/**
 * POST /api/mpesa/stk-push
 * Body: { phone, amount, accountRef?, description? }
 * Sends a real M-Pesa STK push to the given phone via Daraja.
 */
import { stkPush, isConfigured, missingEnvVars } from "../_daraja.js";

export default async function handler(request) {
  if (request.method !== "POST") {
    return json(405, { error: "Method not allowed" });
  }

  if (!isConfigured()) {
    return json(503, {
      success: false,
      error: "M-Pesa is not configured yet. Missing: " + missingEnvVars().join(", "),
    });
  }

  let body;
  try {
    body = await request.json();
  } catch {
    body = null;
  }

  if (!body || !body.phone || !body.amount) {
    return json(400, { success: false, error: "phone and amount are required" });
  }

  try {
    const result = await stkPush({
      phone: body.phone,
      amount: body.amount,
      accountRef: body.accountRef,
      description: body.description,
    });
    const ok = String(result.ResponseCode) === "0";
    if (ok) {
      return json(200, {
        success: true,
        checkoutRequestId: result.CheckoutRequestID,
        merchantRequestId: result.MerchantRequestID,
        message: result.CustomerMessage || result.ResponseDescription || "STK push sent",
      });
    }
    return json(502, {
      success: false,
      error: result.ResponseDescription || result.errorMessage || "M-Pesa request failed",
    });
  } catch (err) {
    return json(502, { success: false, error: err.message || "M-Pesa request failed" });
  }
}

function json(status, data) {
  return new Response(JSON.stringify(data), {
    status,
    headers: { "content-type": "application/json" },
  });
}
