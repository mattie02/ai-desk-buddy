import test from 'node:test';
import assert from 'node:assert/strict';
import { executeTimerTool } from './timer-tools.mjs';

test('invalid tool names, malformed JSON, extra fields and out-of-range durations never dispatch', async () => {
  const request = () => { assert.fail('invalid tool dispatched'); };
  for (const [name, args] of [
    ['unknown', '{}'], ['start_focus_timer', '{'], ['start_focus_timer', 'null'],
    ['start_focus_timer', '{}'], ['start_focus_timer', '{"seconds":1.5}'],
    ['start_focus_timer', '{"seconds":0}'], ['start_focus_timer', '{"seconds":86401}'],
    ['start_focus_timer', '{"seconds":25,"replace":true}'], ['get_focus_timer', '{"remaining":5}'],
  ]) assert.equal((await executeTimerTool(name, args, request)).code, 'invalid_tool_arguments');
});

test('cancel of a completed timer only reads status and never cancels a later timer', async () => {
  let count = 0;
  const result = await executeTimerTool('cancel_focus_timer', '{}', async command => {
    ++count;
    assert.equal(command.type, 'timer_status');
    return { ok: true, timer_id: 'done', running: false, completed: true };
  });
  assert.equal(result.code, 'timer_not_running');
  assert.equal(result.ok, false);
  assert.equal(count, 1);
});

test('offline status prevents a cancellation dispatch', async () => {
  let count = 0;
  const result = await executeTimerTool('cancel_focus_timer', '{}', async () => {
    ++count;
    return { ok: false, code: 'timer_device_offline', outcome: 'not_sent' };
  });
  assert.equal(result.outcome, 'not_sent');
  assert.equal(count, 1);
});
