import test from 'node:test';
import assert from 'node:assert/strict';
import { responseUsage, createUsageTracker } from './usage.mjs';

const model = 'gpt-realtime-2.1';
const usage = () => ({ input_tokens: 300, output_tokens: 60, total_tokens: 360,
  input_token_details: { text_tokens: 100, audio_tokens: 200, image_tokens: 0, cached_tokens: 140,
    cached_tokens_details: { text_tokens: 40, audio_tokens: 100, image_tokens: 0 } },
  output_token_details: { text_tokens: 10, audio_tokens: 50 } });

test('USD estimate separates modalities and subtracts cached input before applying uncached rates', () => {
  const result = responseUsage(usage(), model);
  // 60*4 + 100*32 + 140*0.4 + 10*24 + 50*64 USD / 1M tokens.
  assert.equal(result.estimated_cost_usd, 0.006936);
  assert.equal(result.tokens.cached_input, 140);
  assert.equal(result.tokens.total, 360);
});

test('unknown models, missing breakdowns and invalid cache counts are explicitly unpriced', () => {
  assert.equal(responseUsage(usage(), 'unknown-model').reason, 'model_not_priced');
  assert.equal(responseUsage(null, model).estimated_cost_usd, null);
  assert.equal(responseUsage({ input_tokens: 300, output_tokens: 60 }, model).estimated_cost_usd, null);
  const missingCache = usage(); delete missingCache.input_token_details.cached_tokens_details;
  assert.equal(responseUsage(missingCache, model).estimated_cost_usd, null);
  const invalidCache = usage(); invalidCache.input_token_details.cached_tokens_details.audio_tokens = 250;
  assert.equal(responseUsage(invalidCache, model).estimated_cost_usd, null);
  const negative = usage(); negative.output_token_details.audio_tokens = -50;
  assert.equal(responseUsage(negative, model).estimated_cost_usd, null);
});

test('planning and cancelled speech are counted once; session and process totals survive reconnects', () => {
  const logs = [];
  const tracker = createUsageTracker({ model, log: line => logs.push(line) });
  const first = tracker.session();
  first.begin(1, 'planning');
  first.created({ id: 'plan' });
  const response = { id: 'plan', status: 'completed', usage: usage(), output: [{ text: 'private transcript' }] };
  first.record(response);
  first.record(response);
  first.begin(1, 'reply');
  first.created({ id: 'reply' });
  first.record({ id: 'reply', status: 'cancelled', usage: usage() });
  first.close();
  const second = tracker.session();
  second.begin(1, 'reply');
  second.record({ id: 'next-session', status: 'failed', usage: usage() });
  const report = tracker.snapshot();
  assert.equal(report.totals.responses, 3);
  assert.equal(report.totals.estimated_cost_usd, 0.020808);
  assert.equal(report.sessions[0].totals.estimated_cost_usd, 0.013872);
  assert.equal(report.sessions[0].recent_turns[0].responses, 2);
  assert.equal(report.sessions[1].totals.responses, 1);
  assert.equal(logs.length, 3);
  assert.ok(logs[0].includes('planning'));
  assert.ok(!logs.join('').includes('private transcript'));
});

test('lost final receipts and missing usage keep the total incomplete while retaining known costs', () => {
  const tracker = createUsageTracker({ model, log: () => {} });
  const session = tracker.session();
  session.begin(1, 'reply');
  session.record({ id: 'known', status: 'completed', usage: usage() });
  session.begin(2, 'reply');
  session.record({ id: 'missing', status: 'failed' });
  session.begin(3, 'reply');
  session.close();
  const report = tracker.snapshot();
  assert.equal(report.totals.estimate_complete, false);
  assert.equal(report.totals.estimated_cost_usd, null);
  assert.equal(report.totals.known_estimated_cost_usd, 0.006936);
  assert.equal(report.totals.unreported_responses, 1);
  assert.equal(report.totals.missing_usage, 1);
  assert.equal(report.sessions[0].recent_turns[2].estimated_cost_usd, null);
});

test('usage history is bounded while process totals retain all sessions', () => {
  const tracker = createUsageTracker({ model, log: () => {} });
  for (let i = 0; i < 12; ++i) {
    const session = tracker.session();
    for (let turn = 1; turn <= 12; ++turn) {
      session.begin(turn, 'reply');
      session.record({ id: `response-${turn}`, status: 'completed', usage: usage() });
    }
    session.close();
  }
  const report = tracker.snapshot();
  assert.equal(report.sessions.length, 10);
  assert.equal(report.sessions[9].recent_turns.length, 10);
  assert.equal(report.totals.responses, 144);
  assert.equal(report.totals.estimated_cost_usd, 0.998784);
});
