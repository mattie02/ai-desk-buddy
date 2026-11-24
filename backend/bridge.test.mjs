import test from 'node:test';
import assert from 'node:assert/strict';
import { EventEmitter, once } from 'node:events';
import { WebSocket } from 'ws';
import { createBuddyServer, echoLevelGain } from './bridge.mjs';

const TOKEN = 'test-device-token-with-24-characters';
class Inbox {
  queue = [];
  waiters = [];
  constructor(socket) {
    socket.on('message', (data, binary) => {
      this.queue.push(binary ? Buffer.from(data) : JSON.parse(data.toString()));
      for (const wake of this.waiters.splice(0)) wake();
    });
  }
  async next(predicate) {
    for (;;) {
      const index = this.queue.findIndex(predicate);
      if (index !== -1) return this.queue.splice(index, 1)[0];
      await new Promise(resolve => this.waiters.push(resolve));
    }
  }
  type(type) { return this.next(message => message.type === type); }
}

async function fixture(t, options = {}) {
  const server = createBuddyServer({ host: '127.0.0.1', port: 0, token: TOKEN,
    mode: 'echo', gain: 0.1, log: () => {}, ...options });
  await once(server, 'listening');
  const sockets = [];
  t.after(async () => {
    for (const client of sockets) client.terminate();
    for (const client of server.clients) client.terminate();
    await new Promise(resolve => server.close(resolve));
  });
  async function connect(token = TOKEN, capabilities = {}) {
    const socket = new WebSocket(`ws://127.0.0.1:${server.address().port}/buddy`);
    sockets.push(socket);
    const inbox = new Inbox(socket);
    await once(socket, 'open');
    const send = value => socket.send(JSON.stringify(value));
    send({ type: 'hello', token, sample_rate: 24000, ...capabilities });
    return { socket, inbox, send };
  }
  async function control(command) {
    const socket = new WebSocket(`ws://127.0.0.1:${server.address().port}/control`);
    sockets.push(socket);
    const inbox = new Inbox(socket);
    await once(socket, 'open');
    socket.send(JSON.stringify({ token: TOKEN, ...command }));
    return { socket, inbox };
  }
  return { connect, control };
}

test('timer success waits for matching ESP32 acceptance, independently of OpenAI response creation', { timeout: 5000 }, async t => {
  const { connect, control } = await fixture(t);
  const device = await connect(TOKEN, { timer_support: 1 });
  await device.inbox.type('ready');
  const caller = await control({ type: 'start_timer', request_id: 'timer-a', seconds: 10 });
  const command = await device.inbox.type('start_timer');
  assert.equal(command.seconds, 10);
  await new Promise(resolve => setTimeout(resolve, 40));
  assert.equal(caller.inbox.queue.length, 0, 'dispatch alone must not confirm success');
  device.send({ type: 'timer_result', request_id: 'wrong-id', accepted: true, seconds: 10 });
  device.send({ type: 'timer_result', request_id: 'timer-a', accepted: true, seconds: 10,
    timer_id: 'timer-a', remaining_seconds: 10, running: true, completed: false });
  const receipt = await caller.inbox.next(item => item.request_id === 'timer-a');
  assert.equal(receipt.ok, true);
  assert.equal(receipt.code, 'device_confirmed');
  assert.equal(receipt.running, true);
});

test('timer rejection is returned as rejection and status comes from the device', { timeout: 5000 }, async t => {
  const { connect, control } = await fixture(t);
  const device = await connect(TOKEN, { timer_support: 1 });
  await device.inbox.type('ready');
  const rejected = await control({ type: 'start_timer', request_id: 'timer-b', seconds: 20 });
  await device.inbox.type('start_timer');
  device.send({ type: 'timer_result', request_id: 'timer-b', accepted: false, code: 'timer_already_running', seconds: 10 });
  assert.equal((await rejected.inbox.next(item => item.request_id === 'timer-b')).ok, false);
  const status = await control({ type: 'timer_status', request_id: 'status-a' });
  await device.inbox.type('timer_status');
  device.send({ type: 'timer_status', request_id: 'status-a', timer_id: 'existing', seconds: 10,
    remaining_seconds: 4, running: true, completed: false });
  assert.equal((await status.inbox.next(item => item.request_id === 'status-a')).remaining_seconds, 4);
});

test('lost device acknowledgement produces unknown outcome rather than false failure/success', { timeout: 5000 }, async t => {
  const { connect, control } = await fixture(t);
  const device = await connect(TOKEN, { timer_support: 1 });
  await device.inbox.type('ready');
  const caller = await control({ type: 'start_timer', request_id: 'lost-ack', seconds: 10 });
  await device.inbox.type('start_timer');
  device.socket.terminate();
  const receipt = await caller.inbox.next(item => item.request_id === 'lost-ack');
  assert.equal(receipt.ok, false);
  assert.equal(receipt.outcome, 'unknown');
});

test('invalid or unauthenticated timer commands never reach the device', { timeout: 5000 }, async t => {
  const { connect, control } = await fixture(t);
  const device = await connect(TOKEN, { timer_support: 1 });
  await device.inbox.type('ready');
  const bad = await control({ type: 'start_timer', request_id: 'bad', seconds: 1.5 });
  assert.equal((await bad.inbox.next(item => item.code)).code, 'invalid_timer_command');
  const unauthorized = await control({ type: 'start_timer', request_id: 'bad-token', seconds: 10, token: 'incorrect' });
  assert.equal((await unauthorized.inbox.next(item => item.code)).code, 'unauthorized');
  assert.ok(!device.inbox.queue.some(item => item.type === 'start_timer'));
});

async function finishPlayback(device, turn) {
  const output = [];
  for (;;) {
    const message = await device.inbox.next(item => Buffer.isBuffer(item) || item.type === 'finish');
    if (message.type === 'finish') {
      assert.equal(message.turn, turn);
      device.send({ type: 'playback_done', turn });
      await device.inbox.type('ready');
      return Buffer.concat(output);
    }
    assert.ok(message.length <= 1024);
    output.push(message);
    device.send({ type: 'ack', turn });
  }
}

test('local echo preserves ordered PCM, applies gain, and waits for playback completion', { timeout: 5000 }, async t => {
  const { connect } = await fixture(t, { normalizeEcho: false });
  const device = await connect();
  await device.inbox.type('ready');
  const input = Buffer.alloc(5760);
  for (let i = 0; i < input.length; i += 2) input.writeInt16LE(i % 4 ? -12000 : 15000, i);
  device.send({ type: 'begin', turn: 1 });
  // Firmware now batches ~43 ms (2048 bytes); release flushes the shorter tail.
  device.socket.send(input.subarray(0, 2048));
  device.socket.send(input.subarray(2048, 4096));
  device.socket.send(input.subarray(4096));
  device.send({ type: 'end', turn: 1 });
  await device.inbox.type('status');
  const output = await finishPlayback(device, 1);
  assert.equal(output.length, input.length);
  for (let i = 0; i < output.length; i += 2) assert.equal(output.readInt16LE(i), input.readInt16LE(i) / 10);
  device.send({ type: 'begin', turn: 2 });
  const status = await device.inbox.type('status');
  // Other status events may still be queued, so select the new turn explicitly.
  if (status.turn !== 2) await device.inbox.next(item => item.type === 'status' && item.turn === 2);
});

test('short tap clears input and returns ready without playback', { timeout: 5000 }, async t => {
  const { connect } = await fixture(t);
  const device = await connect();
  await device.inbox.type('ready');
  device.send({ type: 'begin', turn: 1 });
  device.socket.send(Buffer.alloc(200));
  device.send({ type: 'end', turn: 1 });
  assert.equal((await device.inbox.type('notice')).code, 'hold_at_least_100ms');
  await device.inbox.type('ready');
  assert.ok(!device.inbox.queue.some(Buffer.isBuffer));
});

test('playback window buffers ahead, tolerates delayed batched credits, and preserves sample order', { timeout: 5000 }, async t => {
  const { connect } = await fixture(t, { normalizeEcho: false });
  const device = await connect(TOKEN, { playback_window: 16 });
  await device.inbox.type('ready');
  const input = Buffer.alloc(32768);
  for (let i = 0; i < input.length; i += 2) input.writeInt16LE((i / 2) % 2000 - 1000, i);
  device.send({ type: 'begin', turn: 1 });
  for (let i = 0; i < input.length; i += 2048) device.socket.send(input.subarray(i, i + 2048));
  device.send({ type: 'end', turn: 1 });
  const output = [];
  for (let i = 0; i < 16; ++i) output.push(await device.inbox.next(Buffer.isBuffer));
  await new Promise(resolve => setTimeout(resolve, 100));
  assert.equal(device.inbox.queue.filter(Buffer.isBuffer).length, 0, 'server must respect advertised capacity');
  assert.ok(!device.inbox.queue.some(item => item.type === 'finish'));
  device.send({ type: 'ack', turn: 1, count: 8 });
  for (let i = 0; i < 8; ++i) output.push(await device.inbox.next(Buffer.isBuffer));
  device.send({ type: 'ack', turn: 1, count: 16 });
  for (let i = 0; i < 8; ++i) output.push(await device.inbox.next(Buffer.isBuffer));
  await device.inbox.type('finish');
  assert.ok(!device.inbox.queue.some(item => item.type === 'ready'), 'dispatch is not audible completion');
  device.send({ type: 'ack', turn: 1, count: 8 });
  device.send({ type: 'playback_done', turn: 1 });
  await device.inbox.type('ready');
  const pcm = Buffer.concat(output);
  assert.equal(pcm.length, input.length);
  for (let i = 0; i < pcm.length; i += 2) assert.equal(pcm.readInt16LE(i), Math.round(input.readInt16LE(i) * 0.1) || 0);
});

test('short buffered replies receive finish before credits, avoiding prebuffer deadlock', { timeout: 5000 }, async t => {
  const { connect } = await fixture(t);
  const device = await connect(TOKEN, { playback_window: 16 });
  await device.inbox.type('ready');
  device.send({ type: 'begin', turn: 1 });
  device.socket.send(Buffer.alloc(2880));
  device.socket.send(Buffer.alloc(2880));
  device.send({ type: 'end', turn: 1 });
  await device.inbox.type('finish');
  const packets = device.inbox.queue.filter(Buffer.isBuffer);
  assert.ok(packets.length > 0 && packets.length < 10);
  assert.equal(Buffer.concat(packets).length, 5760);
  device.send({ type: 'ack', turn: 1, count: packets.length });
  device.send({ type: 'playback_done', turn: 1 });
  await device.inbox.type('ready');
});

test('device cannot finish playback with unconsumed audio credits', { timeout: 5000 }, async t => {
  const { connect } = await fixture(t);
  const device = await connect(TOKEN, { playback_window: 16 });
  await device.inbox.type('ready');
  device.send({ type: 'begin', turn: 1 });
  device.socket.send(Buffer.alloc(2880));
  device.socket.send(Buffer.alloc(2880));
  device.send({ type: 'end', turn: 1 });
  await device.inbox.type('finish');
  device.send({ type: 'playback_done', turn: 1 });
  assert.equal((await device.inbox.type('error')).code, 'invalid_playback_done');
});

test('quiet microphone speech reaches useful playback level without clipping or per-packet pumping', { timeout: 5000 }, async t => {
  const { connect } = await fixture(t, { gain: 0.5 });
  const device = await connect();
  await device.inbox.type('ready');
  const input = Buffer.alloc(5760);
  for (let i = 0; i < input.length; i += 2) {
    const magnitude = i < 2880 ? 1000 : 2000;
    input.writeInt16LE(i % 4 ? -magnitude : magnitude, i);
  }
  device.send({ type: 'begin', turn: 1 });
  device.socket.send(input.subarray(0, 2880));
  device.socket.send(input.subarray(2880));
  device.send({ type: 'end', turn: 1 });
  const output = await finishPlayback(device, 1);
  assert.equal(output.length, input.length);
  assert.ok(Math.abs(output.readInt16LE(2880) - 13107) <= 1);
  assert.ok(Math.abs(output.readInt16LE(0) * 2 - output.readInt16LE(2880)) <= 1);
  assert.ok(output.readInt16LE(2) < 0);
  for (let i = 0; i < output.length; i += 2) assert.ok(Math.abs(output.readInt16LE(i)) < 32767);
});

test('echo level correction leaves silence/noise alone and never boosts a full-scale input', () => {
  assert.equal(echoLevelGain(Buffer.alloc(200)), 1);
  const quiet = Buffer.alloc(200);
  quiet.writeInt16LE(20);
  assert.equal(echoLevelGain(quiet), 1);
  const loud = Buffer.alloc(200);
  loud.writeInt16LE(-32768);
  assert.equal(echoLevelGain(loud), 1);
});

test('rejects unauthenticated audio without opening Realtime', { timeout: 5000 }, async t => {
  let connected = false;
  const { connect } = await fixture(t, { mode: 'realtime', apiKey: 'dummy', makeRealtime: () => { connected = true; } });
  const device = await connect('incorrect-token');
  assert.equal((await device.inbox.type('error')).code, 'invalid_hello');
  assert.equal(connected, false);
});

test('out-of-order playback acknowledgement fails the turn', { timeout: 5000 }, async t => {
  const { connect } = await fixture(t);
  const device = await connect();
  await device.inbox.type('ready');
  device.send({ type: 'ack', turn: 1 });
  assert.equal((await device.inbox.type('error')).code, 'invalid_playback_ack');
});

class MockRealtime extends EventEmitter {
  readyState = WebSocket.OPEN;
  bufferedAmount = 0;
  events = [];
  constructor() {
    super();
    queueMicrotask(() => this.emitEvent({ type: 'session.created' }));
  }
  emitEvent(event) { this.emit('message', Buffer.from(JSON.stringify(event))); }
  send(text) {
    const event = JSON.parse(text);
    this.events.push(event);
    queueMicrotask(() => {
      if (event.type === 'session.update') this.emitEvent({ type: 'session.updated' });
      if (event.type === 'input_audio_buffer.commit') this.emitEvent({ type: 'input_audio_buffer.committed' });
      if (event.type === 'response.create') {
        this.emitEvent({ type: 'response.output_audio.delta', delta: Buffer.alloc(2048, 1).toString('base64') });
        this.emitEvent({ type: 'response.done', response: { status: 'completed' } });
      }
    });
  }
  terminate() { this.readyState = WebSocket.CLOSED; }
}

test('Realtime config disables VAD and release commits before creating response', { timeout: 5000 }, async t => {
  let upstream;
  const { connect } = await fixture(t, {
    mode: 'realtime', apiKey: 'dummy-not-a-real-key', voice: 'marin', instructions: 'backend personality',
    makeRealtime: () => (upstream = new MockRealtime()),
  });
  const device = await connect();
  await device.inbox.type('ready');
  const session = upstream.events[0].session;
  assert.equal(session.audio.input.turn_detection, null);
  assert.deepEqual(session.audio.input.format, { type: 'audio/pcm', rate: 24000 });
  assert.equal(session.audio.output.voice, 'marin');
  assert.equal(session.instructions, 'backend personality');
  device.send({ type: 'begin', turn: 1 });
  device.socket.send(Buffer.alloc(2400));
  device.socket.send(Buffer.alloc(2400));
  await device.inbox.next(item => item.type === 'status' && item.state === 'Listening');
  assert.ok(!upstream.events.some(event => event.type === 'response.create'));
  device.send({ type: 'end', turn: 1 });
  assert.equal((await finishPlayback(device, 1)).length, 2048);
  assert.deepEqual(upstream.events.map(event => event.type), [
    'session.update', 'input_audio_buffer.clear', 'input_audio_buffer.append', 'input_audio_buffer.append',
    'input_audio_buffer.commit', 'response.create',
  ]);
});

test('upstream failure closes the turn and releases the single-device slot', { timeout: 5000 }, async t => {
  let upstream;
  const { connect } = await fixture(t, { mode: 'realtime', apiKey: 'dummy', makeRealtime: () => (upstream = new MockRealtime()) });
  const device = await connect();
  await device.inbox.type('ready');
  upstream.emitEvent({ type: 'error', error: { code: 'invalid_api_key', message: 'never logged' } });
  assert.equal((await device.inbox.type('error')).code, 'realtime_invalid_api_key');
  const next = await connect();
  await next.inbox.type('ready');
});

class ToolRealtime extends MockRealtime {
  constructor(call) { super(); this.call = call; }
  send(text) {
    const event = JSON.parse(text);
    this.events.push(event);
    this.emit('sent');
    queueMicrotask(() => {
      if (event.type === 'session.update') this.emitEvent({ type: 'session.updated' });
      if (event.type === 'input_audio_buffer.commit') this.emitEvent({ type: 'input_audio_buffer.committed' });
      if (event.type === 'response.create' && event.response?.output_modalities[0] === 'text') {
        // An unexpected planning audio delta must never reach the speaker.
        this.emitEvent({ type: 'response.output_audio.delta', delta: Buffer.alloc(100).toString('base64') });
        this.emitEvent({ type: 'response.done', response: { id: 'plan-1', status: 'completed', output: this.call ? [this.call] : [] } });
      } else if (event.type === 'response.create') {
        this.emitEvent({ type: 'response.output_audio.delta', delta: Buffer.alloc(2048, 1).toString('base64') });
        // Repeated planning completion must not dispatch again or finish playback.
        this.emitEvent({ type: 'response.done', response: { id: 'plan-1', status: 'completed', output: this.call ? [this.call] : [] } });
        this.emitEvent({ type: 'response.done', response: { id: 'answer-1', status: 'completed', output: [] } });
      }
    });
  }
  async waitFor(predicate) {
    for (;;) {
      const event = this.events.find(predicate);
      if (event) return event;
      await once(this, 'sent');
    }
  }
}
async function toolFixture(t, name, args = {}) {
  let upstream;
  const call = name ? { type: 'function_call', call_id: 'call_test', name, arguments: JSON.stringify(args) } : null;
  const { connect } = await fixture(t, { mode: 'realtime', apiKey: 'dummy', instructions: 'test personality',
    makeRealtime: () => (upstream = new ToolRealtime(call)) });
  const device = await connect(TOKEN, { timer_support: 1, timer_cancel_support: 1, playback_window: 16 });
  await device.inbox.type('ready');
  device.send({ type: 'begin', turn: 1 });
  device.socket.send(Buffer.alloc(2400));
  device.socket.send(Buffer.alloc(2400));
  device.send({ type: 'end', turn: 1 });
  return { device, upstream };
}
const toolOutput = event => event.type === 'conversation.item.create' && event.item.type === 'function_call_output';

test('spoken start cannot speak or confirm until the matching device receipt; duplicate completion executes once', { timeout: 5000 }, async t => {
  const { device, upstream } = await toolFixture(t, 'start_focus_timer', { seconds: 25 });
  const command = await device.inbox.type('start_timer');
  assert.equal(command.seconds, 25);
  assert.equal(upstream.events[0].session.tools.length, 3);
  await new Promise(resolve => setTimeout(resolve, 40));
  assert.equal(upstream.events.filter(event => event.type === 'response.create').length, 1);
  assert.ok(!upstream.events.some(toolOutput));
  assert.ok(!device.inbox.queue.some(Buffer.isBuffer));
  device.send({ type: 'timer_result', request_id: command.request_id, timer_id: command.request_id,
    accepted: true, seconds: 25, remaining_seconds: 25, running: true, completed: false, cancelled: false });
  const result = JSON.parse((await upstream.waitFor(toolOutput)).item.output);
  assert.equal(result.code, 'device_confirmed');
  assert.equal((await finishPlayback(device, 1)).length, 2048);
  assert.equal(upstream.events.filter(toolOutput).length, 1);
  assert.equal(upstream.events.filter(event => event.type === 'response.create').length, 2);
  assert.ok(!device.inbox.queue.some(item => item.type === 'start_timer'));
});

test('spoken status uses the ESP32 remaining time', { timeout: 5000 }, async t => {
  const { device, upstream } = await toolFixture(t, 'get_focus_timer');
  const command = await device.inbox.type('timer_status');
  device.send({ type: 'timer_status', request_id: command.request_id, timer_id: 'active-timer',
    seconds: 1500, remaining_seconds: 123, running: true, completed: false });
  const result = JSON.parse((await upstream.waitFor(toolOutput)).item.output);
  assert.equal(result.remaining_seconds, 123);
  assert.equal(result.remaining_spoken, '2 minutes and 3 seconds');
  assert.equal((await finishPlayback(device, 1)).length, 2048);
});

test('spoken cancel targets the timer read from the device and waits for cancellation acceptance', { timeout: 5000 }, async t => {
  const { device, upstream } = await toolFixture(t, 'cancel_focus_timer');
  const status = await device.inbox.type('timer_status');
  device.send({ type: 'timer_status', request_id: status.request_id, timer_id: 'cancel-target',
    seconds: 30, remaining_seconds: 12, running: true, completed: false });
  const cancel = await device.inbox.type('cancel_timer');
  assert.equal(cancel.timer_id, 'cancel-target');
  assert.ok(!upstream.events.some(toolOutput));
  device.send({ type: 'timer_cancel_result', request_id: cancel.request_id, timer_id: 'cancel-target',
    accepted: true, seconds: 30, remaining_seconds: 0, running: false, completed: false, cancelled: true });
  assert.equal(JSON.parse((await upstream.waitFor(toolOutput)).item.output).cancelled, true);
  assert.equal((await finishPlayback(device, 1)).length, 2048);
});

test('busy device rejection is passed to speech generation without claiming success', { timeout: 5000 }, async t => {
  const { device, upstream } = await toolFixture(t, 'start_focus_timer', { seconds: 25 });
  const command = await device.inbox.type('start_timer');
  device.send({ type: 'timer_result', request_id: command.request_id, accepted: false, code: 'timer_already_running' });
  const result = JSON.parse((await upstream.waitFor(toolOutput)).item.output);
  assert.equal(result.ok, false);
  assert.equal(result.code, 'timer_already_running');
  await finishPlayback(device, 1);
});

test('lost spoken start receipt returns unknown after the deadline, without retrying the action', { timeout: 8000 }, async t => {
  const { device, upstream } = await toolFixture(t, 'start_focus_timer', { seconds: 25 });
  await device.inbox.type('start_timer');
  const result = JSON.parse((await upstream.waitFor(toolOutput)).item.output);
  assert.equal(result.ok, false);
  assert.equal(result.outcome, 'unknown');
  assert.equal(result.code, 'device_ack_timeout');
  await finishPlayback(device, 1);
  assert.ok(!device.inbox.queue.some(item => item.type === 'start_timer'));
});

test('ordinary conversation passes the silent plan and still uses buffered audio completion', { timeout: 5000 }, async t => {
  const { device, upstream } = await toolFixture(t, null);
  assert.equal((await finishPlayback(device, 1)).length, 2048);
  assert.ok(!upstream.events.some(toolOutput));
});

test('malformed accepted cancellation cannot confirm success', { timeout: 5000 }, async t => {
  const { connect, control } = await fixture(t);
  const device = await connect(TOKEN, { timer_support: 1, timer_cancel_support: 1 });
  await device.inbox.type('ready');
  const caller = await control({ type: 'cancel_timer', request_id: 'cancel-request', timer_id: 'target' });
  await device.inbox.type('cancel_timer');
  device.send({ type: 'timer_cancel_result', request_id: 'cancel-request', accepted: true, timer_id: 'different',
    seconds: 30, remaining_seconds: 0, running: false, completed: false, cancelled: true });
  assert.equal((await device.inbox.type('error')).code, 'invalid_timer_receipt');
  const result = await caller.inbox.next(item => item.request_id === 'cancel-request');
  assert.equal(result.ok, false);
  assert.equal(result.outcome, 'unknown');
});

class ChunkedRealtime extends MockRealtime {
  constructor(chunks) { super(); this.chunks = chunks; this.cancelled = false; }
  send(text) {
    const event = JSON.parse(text);
    this.events.push(event);
    queueMicrotask(() => {
      if (event.type === 'session.update') this.emitEvent({ type: 'session.updated' });
      if (event.type === 'input_audio_buffer.commit') this.emitEvent({ type: 'input_audio_buffer.committed' });
      if (event.type === 'response.create') {
        for (const chunk of this.chunks) this.emitEvent({ type: 'response.output_audio.delta', delta: chunk.toString('base64') });
        if (!this.cancelled) this.emitEvent({ type: 'response.done', response: { status: 'completed' } });
      }
      if (event.type === 'response.cancel') this.emitEvent({ type: 'response.done', response: { status: 'cancelled' } });
    });
    if (event.type === 'response.cancel') this.cancelled = true;
  }
}
async function chunkedTurn(t, chunks) {
  let upstream;
  const { connect } = await fixture(t, { mode: 'realtime', apiKey: 'dummy',
    makeRealtime: () => (upstream = new ChunkedRealtime(chunks)) });
  const device = await connect(TOKEN, { playback_window: 32 });
  await device.inbox.type('ready');
  device.send({ type: 'begin', turn: 1 });
  device.socket.send(Buffer.alloc(2400));
  device.socket.send(Buffer.alloc(2400));
  device.send({ type: 'end', turn: 1 });
  return { device, upstream };
}

test('tiny provider deltas become full playback packets with only one short final tail', { timeout: 5000 }, async t => {
  const pcm = Buffer.alloc(5760);
  for (let i = 0; i < pcm.length; i += 2) pcm.writeInt16LE(i - 2880, i);
  const chunks = [];
  for (let i = 0; i < pcm.length; i += 96) chunks.push(pcm.subarray(i, i + 96));
  const { device } = await chunkedTurn(t, chunks);
  await device.inbox.type('finish');
  const packets = device.inbox.queue.filter(Buffer.isBuffer);
  assert.deepEqual(packets.map(packet => packet.length), [1024, 1024, 1024, 1024, 1024, 640]);
  const output = Buffer.concat(packets);
  for (let i = 0; i < pcm.length; i += 2) assert.equal(output.readInt16LE(i), Math.round(pcm.readInt16LE(i) * 0.1) || 0);
  device.send({ type: 'ack', turn: 1, count: packets.length });
  device.send({ type: 'playback_done', turn: 1 });
  await device.inbox.type('ready');
});

test('32-packet window stays bounded and an overlong response ends without disconnecting the device', { timeout: 8000 }, async t => {
  const limit = 24000 * 2 * 30;
  const { device, upstream } = await chunkedTurn(t, [Buffer.alloc(limit + 2048, 1), Buffer.alloc(2048, 2)]);
  await device.inbox.type('notice');
  await new Promise(resolve => setTimeout(resolve, 40));
  assert.equal(device.inbox.queue.filter(Buffer.isBuffer).length, 32);
  assert.equal(upstream.events.filter(event => event.type === 'response.cancel').length, 1);
  assert.equal((await finishPlayback(device, 1)).length, limit);
  assert.equal(device.socket.readyState, WebSocket.OPEN);
  const cancel = upstream.events.find(event => event.type === 'response.cancel');
  upstream.emitEvent({ type: 'error', error: { event_id: cancel.event_id, code: 'response_cancel_not_active' } });
  device.send({ type: 'begin', turn: 2 });
  await device.inbox.next(item => item.type === 'status' && item.turn === 2 && item.state === 'Listening');
});
