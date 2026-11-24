// USD per million tokens, verified against the official model/pricing pages.
export const pricing = Object.freeze({
  model: 'gpt-realtime-2.1', checked_on: '2026-11-23',
  source: 'https://developers.openai.com/api/docs/models/gpt-realtime-2.1',
  input: { text: 4, audio: 32, image: 5 },
  cached: { text: 0.4, audio: 0.4, image: 0.5 },
  output: { text: 24, audio: 64 },
});
const count = value => Number.isSafeInteger(value) && value >= 0;
const rounded = value => Math.round(value * 1e12) / 1e12;

export function responseUsage(usage, model) {
  if (!usage || !count(usage.input_tokens) || !count(usage.output_tokens)) {
    return { tokens: null, estimated_cost_usd: null, reason: 'usage_missing_or_invalid' };
  }
  const tokens = { input: usage.input_tokens, output: usage.output_tokens,
    total: usage.input_tokens + usage.output_tokens };
  const input = usage.input_token_details;
  const output = usage.output_token_details;
  const cached = input?.cached_tokens_details;
  const modalities = ['text', 'audio', 'image'];
  const values = (details, names) => names.map(name => details?.[`${name}_tokens`] ?? 0);
  const incoming = values(input, modalities);
  const outgoing = values(output, ['text', 'audio']);
  const cacheTotal = input?.cached_tokens;
  const caches = values(cached, modalities);
  if (!input || !output || !count(cacheTotal) ||
      ![...incoming, ...outgoing, ...caches].every(count) ||
      incoming.reduce((a, b) => a + b, 0) !== tokens.input ||
      outgoing.reduce((a, b) => a + b, 0) !== tokens.output ||
      caches.reduce((a, b) => a + b, 0) !== cacheTotal ||
      caches.some((value, i) => value > incoming[i])) {
    return { tokens, estimated_cost_usd: null, reason: 'token_breakdown_missing_or_invalid' };
  }
  tokens.input_text = incoming[0]; tokens.input_audio = incoming[1]; tokens.input_image = incoming[2];
  tokens.cached_input = cacheTotal;
  tokens.cached_text = caches[0]; tokens.cached_audio = caches[1]; tokens.cached_image = caches[2];
  tokens.output_text = outgoing[0]; tokens.output_audio = outgoing[1];
  if (model !== pricing.model) return { tokens, estimated_cost_usd: null, reason: 'model_not_priced' };
  let cost = 0;
  modalities.forEach((name, i) => {
    cost += (incoming[i] - caches[i]) * pricing.input[name] + caches[i] * pricing.cached[name];
  });
  cost += outgoing[0] * pricing.output.text + outgoing[1] * pricing.output.audio;
  return { tokens, estimated_cost_usd: rounded(cost / 1e6), reason: null };
}

function totals() {
  return { requests: 0, responses: 0, missing_usage: 0, unpriced_responses: 0,
    input_tokens: 0, output_tokens: 0, cached_input_tokens: 0, known_estimated_cost_usd: 0 };
}
function summary(value) {
  const unreported = Math.max(0, value.requests - value.responses);
  const complete = !unreported && !value.missing_usage && !value.unpriced_responses;
  return { ...value, unreported_responses: unreported, estimate_complete: complete,
    estimated_cost_usd: complete ? value.known_estimated_cost_usd : null };
}
function add(target, result) {
  ++target.responses;
  if (!result.tokens) ++target.missing_usage;
  else {
    target.input_tokens += result.tokens.input;
    target.output_tokens += result.tokens.output;
    target.cached_input_tokens += result.tokens.cached_input || 0;
  }
  if (result.estimated_cost_usd === null) ++target.unpriced_responses;
  else target.known_estimated_cost_usd = rounded(target.known_estimated_cost_usd + result.estimated_cost_usd);
}

export function createUsageTracker({ model, log = console.log }) {
  const startedAt = new Date().toISOString();
  const processTotals = totals();
  const sessions = [];
  let nextSession = 0;
  return {
    snapshot() {
      return { model, started_at: startedAt, currency: 'USD', scope: 'this_backend_process',
        pricing: model === pricing.model ? pricing : null, totals: summary(processTotals),
        sessions: sessions.map(session => session.snapshot()) };
    },
    session() {
      const id = ++nextSession;
      const sessionTotals = totals();
      const pending = [];
      const inFlight = new Map();
      const seen = new Set();
      const turns = new Map();
      let closed = false;
      function turnTotals(turn) {
        if (!turns.has(turn)) {
          turns.set(turn, totals());
          if (turns.size > 10) turns.delete(turns.keys().next().value);
        }
        return turns.get(turn);
      }
      const session = {
        begin(turn, stage) {
          const entry = { turn, stage };
          pending.push(entry);
          for (const total of [processTotals, sessionTotals, turnTotals(turn)]) ++total.requests;
        },
        created(response) {
          if (typeof response?.id === 'string' && !seen.has(response.id) && !inFlight.has(response.id) && pending.length) {
            inFlight.set(response.id, pending.shift());
          }
        },
        record(response) {
          if (typeof response?.id !== 'string' || seen.has(response.id)) return;
          // Realtime sessions have a finite lifetime; retain a bounded duplicate window.
          if (seen.size >= 4096) seen.delete(seen.values().next().value);
          seen.add(response.id);
          const entry = inFlight.get(response.id) || pending.shift() || { turn: null, stage: 'unattributed' };
          inFlight.delete(response.id);
          const result = responseUsage(response.usage, model);
          const turn = turnTotals(entry.turn);
          for (const total of [processTotals, sessionTotals, turn]) add(total, result);
          log(`Usage ${JSON.stringify({ session: id, turn: entry.turn, stage: entry.stage,
            status: ['completed', 'cancelled', 'failed', 'incomplete'].includes(response.status) ? response.status : 'unknown',
            ...result, turn_totals: summary(turn), session_totals: summary(sessionTotals) })}`);
        },
        close() { closed = true; },
        snapshot() { return { session: id, closed, totals: summary(sessionTotals),
          recent_turns: [...turns].map(([turn, value]) => ({ turn, ...summary(value) })) }; },
      };
      sessions.push(session);
      if (sessions.length > 10) sessions.shift();
      return session;
    },
  };
}
