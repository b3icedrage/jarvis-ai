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
    // Log the real reason for the owner; keep the customer-facing message friendly.
    console.error("MPESA_STK_PUSH_NOT_CONFIGURED missing=" + missingEnvVars().join(","));
    return json(503, {
      success: false,
      error: "Payments are temporarily unavailable. Please try again later.",
    });
  }

  let body;
  try {
    body = await request.json();
  } catch {
    body = null;
  }

  if (!body || !body.phone || !body.amount) {
    return json(400, { success: false, error: "Please provide a phone number and amount." });
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
    console.error("MPESA_STK_PUSH_FAILED " + JSON.stringify({
      responseCode: result.ResponseCode,
      description: result.ResponseDescription || result.errorMessage || "unknown",
    }));
    return json(502, {
      success: false,
      error: "Payment couldn't be completed. Please try again.",
    });
  } catch (err) {
    // Input errors get an honest, friendly nudge; everything else is a generic failure.
    const friendly = String(err.message || "").startsWith("Invalid")
      ? "Please check the phone number or amount and try again."
      : "Payment couldn't be completed. Please try again.";
    console.error("MPESA_STK_PUSH_ERROR " + (err && err.message ? err.message : String(err)));
    return json(502, { success: false, error: friendly });
  }
}

function json(status, data) {
  return new Response(JSON.stringify(data), {
    status,
    headers: { "content-type": "application/json" },
  });
}
