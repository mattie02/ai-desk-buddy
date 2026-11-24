# API usage and cost estimates

[Documentation home](Home.md) · [Setup and operation](Setup-and-Operation.md)

From `backend/`, with the backend running:

```sh
npm run usage
npm run usage -- --json
```

The first command prints a readable report. The second returns structured JSON, including the pricing snapshot. The CLI reads the existing local device token and uses the authenticated, loopback-only `/control` endpoint. It makes no OpenAI request and does not need the API key.

## What is counted

Each Realtime `response.done` receipt contributes its reported input and output tokens. The silent text planning response and final spoken response are counted separately and combined under the same voice turn. Input can include conversation history and previously generated audio/text; these counts are not just the latest held recording. Cancelled, incomplete, and failed responses are included when their final usage arrives. Duplicate response IDs are ignored within the bounded duplicate window.

The backend prints a numeric `Usage` log for each final response, with planning/reply stage, status, token breakdown, estimated USD cost, and turn/session totals. No prompts, transcripts, audio, API keys, device tokens, or raw response payloads are included in those records.

The report includes process totals and the last ten Realtime sessions, each with its last ten turns. Session totals survive a device reconnect in the process history; process totals retain all sessions observed by that backend process. **Everything resets when the backend restarts.** There is no persistent spending ledger, account-wide billing query, budget enforcement, or historical backfill.

## Pricing snapshot

Standard rates for the configured `gpt-realtime-2.1` model, checked November 23, 2026; USD per million tokens:

| Modality | Uncached input | Cached input | Output |
| --- | --- | --- | --- |
| Text | $4.00 | $0.40 | $24.00 |
| Audio | $32.00 | $0.40 | $64.00 |
| Image | $5.00 | $0.50 | Not used |

Sources: [official model page](https://developers.openai.com/api/docs/models/gpt-realtime-2.1), [official pricing](https://developers.openai.com/api/docs/pricing). Rates are versioned in `backend/usage.mjs`; verify and update them if model/pricing changes. Unknown model names are not automatically assigned another model's prices.

Cached input tokens are already included in input totals. The estimator subtracts the cached counts by modality before applying the uncached rates, then adds cached input and output charges. It preserves precision internally and displays USD estimates to six decimal places.

The [Realtime event reference](https://developers.openai.com/api/reference/resources/realtime/server-events) defines the final usage fields and explains how conversation context contributes to subsequent input.

## Incomplete estimates

Missing usage, an incomplete/invalid modality or cache breakdown, or an unknown model price makes that response unpriced. A request still awaiting its final response is also marked unreported. The total `estimated_cost_usd` is then null and `estimate_complete` is false; `known_estimated_cost_usd` retains the priced subtotal. A missing receipt is never treated as free usage.

An in-progress turn's report only covers requests issued so far; it can grow when the final spoken response is requested. If a connection drops before final usage arrives, actual billing may exceed the known subtotal. Echo mode and typed timer/status/usage commands do not generate API responses.

These are estimates for this process, not an invoice or remaining API-credit balance. Published rates may change, and account-specific billing, credits, discounts, taxes, or other applications' usage are outside this report. Check the OpenAI billing dashboard for the account's billed totals.

## Verification

All 35 automated tests pass, covering mixed text/audio cached pricing, missing/invalid usage, unknown models, duplicate events, cancellation/failure usage, reconnect totals, bounded history, authenticated reporting, and existing audio/timer regressions. The local CLI starts and reads the live backend. Live verification passed: the user confirmed hearing the reply, and the authenticated report returned a complete estimate covering planning and speech. Actual usage and cost records remain local and are not published in this wiki.
