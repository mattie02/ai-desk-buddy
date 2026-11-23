import { timingSafeEqual } from 'node:crypto';
import { WebSocket, WebSocketServer } from 'ws';

const RATE = 24000;
const MAX_TURN_BYTES = RATE * 2 * 15;
const MAX_REPLY_BYTES = RATE * 2 * 30;
const PACKET_BYTES = 1024;

export function sessionUpdate({ model, voice, instructions }) {
  return { type: 'session.update', session: {
    type: 'realtime', model, instructions, output_modalities: ['audio'],
    audio: {
      input: { format: { type: 'audio/pcm', rate: RATE }, turn_detection: null },
      output: { format: { type: 'audio/pcm', rate: RATE }, voice },
    },
  } };
}

export function attenuate(pcm, gain) {
  if (pcm.length % 2) throw new Error('unaligned_pcm');
  const out = Buffer.allocUnsafe(pcm.length);
  for (let i = 0; i < pcm.length; i += 2) {
    out.writeInt16LE(Math.round(pcm.readInt16LE(i) * gain), i);
  }
  return out;
}

export function echoLevelGain(pcm) {
  if (pcm.length % 2) throw new Error('unaligned_pcm');
  let peak = 0;
  for (let i = 0; i < pcm.length; i += 2) peak = Math.max(peak, Math.abs(pcm.readInt16LE(i)));
  // Don't boost room noise or silence. Use one gain for the entire turn, avoiding pumping.
  if (peak < 327.68) return 1;
  return Math.max(1, Math.min(16, (32767 * 0.8) / peak));
}

export function createBuddyServer({
  host = '127.0.0.1', port = 8765, token, apiKey,
  model = 'gpt-realtime-2.1', voice = 'marin', instructions,
  gain = 0.06, mode = 'realtime', normalizeEcho = true, log = console.log,
  makeRealtime = () => new WebSocket(`wss://api.openai.com/v1/realtime?model=${encodeURIComponent(model)}`, {
    headers: { Authorization: `Bearer ${apiKey}` }, handshakeTimeout: 15000, maxPayload: 2 * 1024 * 1024,
  }),
}) {
  if (!token || token.length < 24) throw new Error('DEVICE_TOKEN must contain at least 24 characters');
  if (!['echo', 'realtime'].includes(mode)) throw new Error('BACKEND_MODE must be echo or realtime');
  if (mode === 'realtime' && !apiKey) throw new Error('Set OPENAI_API_KEY locally, or use BACKEND_MODE=echo');
  if (!Number.isFinite(gain) || gain < 0 || gain > 1) throw new Error('OUTPUT_GAIN must be between 0 and 1');
  const server = new WebSocketServer({ host, port, maxPayload: 4096 });
  let active = null;
  let timerDevice = null;
  const pendingTimers = new Map();
  const validId = id => typeof id === 'string' && /^[a-zA-Z0-9_-]{1,64}$/.test(id);
  const validToken = value => {
    const provided = Buffer.from(typeof value === 'string' ? value : '');
    const expected = Buffer.from(token);
    return provided.length === expected.length && timingSafeEqual(provided, expected);
  };
  function timerReply(id, result) {
    const pending = pendingTimers.get(id);
    if (!pending) return;
    clearTimeout(pending.timeout);
    pendingTimers.delete(id);
    pending.reply(result);
  }
  server.on('connection', (device, request) => {
    if (request.url === '/control') {
      const address = request.socket.remoteAddress;
      if (!['127.0.0.1', '::1', '::ffff:127.0.0.1'].includes(address)) { device.close(1008, 'localhost_only'); return; }
      let used = false;
      const timeout = setTimeout(() => device.close(1008, 'control_timeout'), 5000);
      device.on('error', () => {});
      device.on('close', () => clearTimeout(timeout));
      device.on('message', (data, binary) => {
        if (used) return;
        used = true;
        clearTimeout(timeout);
        const reply = result => {
          if (device.readyState === WebSocket.OPEN) {
            device.send(JSON.stringify(result));
            device.close();
          }
        };
        let command;
        try { if (binary) throw new Error(); command = JSON.parse(data.toString()); }
        catch { return reply({ ok: false, code: 'invalid_control' }); }
        const { type, request_id: id, seconds } = command;
        if (!validToken(command.token)) return reply({ ok: false, code: 'unauthorized' });
        if (!validId(id) || !['start_timer', 'timer_status'].includes(type) ||
            (type === 'start_timer' && (!Number.isInteger(seconds) || seconds < 1 || seconds > 86400))) {
          return reply({ ok: false, code: 'invalid_timer_command' });
        }
        if (!timerDevice || timerDevice.readyState !== WebSocket.OPEN) return reply({ ok: false, code: 'timer_device_offline', request_id: id, outcome: 'not_sent' });
        if (pendingTimers.has(id) || pendingTimers.size >= 16) return reply({ ok: false, code: 'command_pending', request_id: id });
        const destination = timerDevice;
        const pending = { device: destination, reply, type, seconds,
          timeout: setTimeout(() => timerReply(id, { ok: false, code: 'device_ack_timeout', request_id: id, outcome: 'unknown' }), 5000) };
        pendingTimers.set(id, pending);
        destination.send(JSON.stringify({ type, request_id: id, ...(type === 'start_timer' ? { seconds } : {}) }), error => {
          if (error) timerReply(id, { ok: false, code: 'device_send_failed', request_id: id, outcome: 'unknown' });
        });
      });
      return;
    }
    if (request.url !== '/buddy') { device.close(1008, 'unknown_path'); return; }
    let upstream;
    let phase = 'auth';
    let turn = 0;
    let bytes = 0;
    let turnTimer;
    let echoChunks = [];
    let queue = [];
    let queuedBytes = 0;
    let replyBytes = 0;
    let inFlight = 0;
    let playbackWindow = 1; // Backward compatible with the original device firmware.
    let replyDone = false;
    let finishSent = false;
    let ackTimer;
    let alive = true;
    const heartbeat = setInterval(() => {
      if (!alive) { fail('device_heartbeat_timeout'); device.terminate(); return; }
      alive = false;
      device.ping();
    }, 10000);
    heartbeat.unref();
    device.on('pong', () => { alive = true; });
    const authTimer = setTimeout(() => fail('hello_timeout'), 10000);
    const send = value => {
      if (device.readyState === WebSocket.OPEN) device.send(JSON.stringify(value));
    };
    function fail(code) {
      if (phase === 'closed') return;
      log(`Device session ended: ${code}`);
      send({ type: 'error', code });
      phase = 'closed';
      clearTimeout(authTimer);
      clearTimeout(turnTimer);
      clearTimeout(ackTimer);
      clearInterval(heartbeat);
      queue = [];
      echoChunks = [];
      if (active === device) active = null;
      device.close(1011, code.slice(0, 100));
      setTimeout(() => device.terminate(), 500).unref();
      upstream?.terminate();
    }
    function api(event) {
      if (upstream?.readyState !== WebSocket.OPEN) return fail('realtime_disconnected');
      if (upstream.bufferedAmount > 256 * 1024) return fail('upstream_too_slow');
      upstream.send(JSON.stringify(event));
    }
    function idle() {
      clearTimeout(turnTimer);
      clearTimeout(ackTimer);
      ackTimer = undefined;
      phase = 'idle';
      bytes = 0;
      echoChunks = [];
      send({ type: 'ready', sample_rate: RATE, mode });
    }
    function pump() {
      if (phase !== 'responding') return;
      while (queue.length && inFlight < playbackWindow) {
        const chunk = queue.shift();
        queuedBytes -= chunk.length;
        ++inFlight;
        device.send(chunk, { binary: true });
      }
      // Signal end-of-audio after dispatch, even with outstanding credits. This
      // lets a short reply start before it has filled the device's prebuffer.
      if (!queue.length && replyDone && !finishSent) {
        finishSent = true;
        send({ type: 'finish', turn });
      }
      if (!ackTimer && (inFlight || finishSent)) {
        ackTimer = setTimeout(() => fail(inFlight ? 'playback_ack_timeout' : 'playback_done_timeout'), 5000);
      }
    }
    function enqueue(pcm, levelGain = 1) {
      if (pcm.length % 2) return fail('invalid_reply_pcm');
      replyBytes += pcm.length;
      if (replyBytes > MAX_REPLY_BYTES || queuedBytes + pcm.length > MAX_REPLY_BYTES) return fail('reply_too_long');
      if (replyBytes === pcm.length && pcm.length) send({ type: 'status', state: 'Speaking', turn });
      const quiet = attenuate(pcm, gain * levelGain);
      for (let i = 0; i < quiet.length; i += PACKET_BYTES) {
        const packet = quiet.subarray(i, i + PACKET_BYTES);
        queue.push(packet);
        queuedBytes += packet.length;
      }
      pump();
    }
    function handleUpstream(data) {
      let event;
      try { event = JSON.parse(data.toString()); } catch { return fail('invalid_realtime_json'); }
      if (phase === 'closed') return;
      switch (event.type) {
        case 'session.created':
          api(sessionUpdate({ model, voice, instructions }));
          break;
        case 'session.updated':
          if (phase === 'connecting') {
            log(`Realtime session configured (${model}, voice ${voice})`);
            idle();
          }
          break;
        case 'input_audio_buffer.committed':
          if (phase === 'committing') {
            phase = 'responding';
            api({ type: 'response.create' });
          }
          break;
        case 'response.output_audio.delta':
          if (phase === 'responding') enqueue(Buffer.from(event.delta, 'base64'));
          break;
        case 'response.done':
          if (phase !== 'responding') break;
          if (event.response?.status !== 'completed') return fail('response_not_completed');
          replyDone = true;
          pump();
          break;
        case 'error':
          // Log codes only: don't retain user audio, transcripts, API payloads, or secrets.
          fail(`realtime_${String(event.error?.code || 'error').replace(/[^a-zA-Z0-9_]/g, '').slice(0, 60)}`);
          break;
      }
    }
    device.on('message', (data, binary) => {
      if (phase === 'closed') return;
      if (binary) {
        if (phase !== 'recording' || !data.length || data.length % 2) return fail('unexpected_input_audio');
        bytes += data.length;
        if (bytes > MAX_TURN_BYTES) return fail('hold_limit_exceeded');
        if (mode === 'echo') echoChunks.push(Buffer.from(data));
        else api({ type: 'input_audio_buffer.append', audio: data.toString('base64') });
        return;
      }
      let message;
      try { message = JSON.parse(data.toString()); } catch { return fail('invalid_device_json'); }
      if (phase === 'auth') {
        const supplied = Buffer.from(typeof message.token === 'string' ? message.token : '');
        const expected = Buffer.from(token);
        if (message.type !== 'hello' || message.sample_rate !== RATE || supplied.length !== expected.length ||
            !timingSafeEqual(supplied, expected)) return fail('invalid_hello');
        const requestedWindow = message.playback_window ?? 1;
        if (!Number.isInteger(requestedWindow) || requestedWindow < 1 || requestedWindow > 16) return fail('invalid_playback_window');
        playbackWindow = requestedWindow;
        if (active) return fail('device_already_connected');
        active = device;
        if (message.timer_support === 1) timerDevice = device;
        clearTimeout(authTimer);
        phase = 'connecting';
        turnTimer = setTimeout(() => fail('realtime_setup_timeout'), 20000);
        log(`Device authenticated (${mode}, PCM ${RATE} Hz, playback window ${playbackWindow})`);
        if (mode === 'echo') return idle();
        try { upstream = makeRealtime(); } catch { return fail('realtime_connect_failed'); }
        upstream.on('message', handleUpstream);
        upstream.on('unexpected-response', (_request, response) => {
          response.resume();
          fail(`realtime_http_${response.statusCode}`);
        });
        upstream.on('error', () => fail('realtime_transport_error'));
        upstream.on('close', () => fail('realtime_closed'));
        return;
      }
      if (message.type === 'timer_result' || message.type === 'timer_status') {
        const pending = pendingTimers.get(message.request_id);
        if (!pending || pending.device !== device) return;
        if (message.type !== (pending.type === 'start_timer' ? 'timer_result' : 'timer_status')) return fail('invalid_timer_receipt');
        if (pending.type === 'start_timer' && (typeof message.accepted !== 'boolean' ||
            (message.accepted && message.seconds !== pending.seconds))) return fail('invalid_timer_receipt');
        const accepted = pending.type === 'timer_status' || message.accepted;
        timerReply(message.request_id, {
          ok: accepted, code: accepted ? 'device_confirmed' : message.code || 'device_rejected',
          request_id: message.request_id, timer_id: message.timer_id,
          seconds: message.seconds, remaining_seconds: message.remaining_seconds,
          running: message.running, completed: message.completed, duplicate: message.duplicate || false,
        });
        if (accepted && pending.type === 'start_timer') log(`Timer accepted by ESP32: ${message.request_id}, ${message.seconds} seconds`);
        return;
      }
      if (message.type === 'timer_finished') {
        log(`Timer finished on ESP32: ${message.timer_id}`);
        return;
      }
      if (message.type === 'begin') {
        if (phase !== 'idle' || !Number.isSafeInteger(message.turn) || message.turn <= turn) return fail('invalid_begin');
        turn = message.turn;
        phase = 'recording';
        bytes = 0;
        replyBytes = 0;
        queuedBytes = 0;
        queue = [];
        replyDone = finishSent = false;
        inFlight = 0;
        turnTimer = setTimeout(() => fail('hold_timeout'), 17000);
        if (mode === 'realtime') api({ type: 'input_audio_buffer.clear' });
        send({ type: 'status', state: 'Listening', turn });
      } else if (message.type === 'end') {
        if (phase !== 'recording' || message.turn !== turn) return fail('invalid_end');
        clearTimeout(turnTimer);
        if (bytes < RATE * 2 / 10) {
          if (mode === 'realtime') api({ type: 'input_audio_buffer.clear' });
          send({ type: 'notice', code: 'hold_at_least_100ms' });
          return idle();
        }
        turnTimer = setTimeout(() => fail('reply_timeout'), 60000);
        send({ type: 'status', state: 'Thinking', turn });
        log(`Turn ${turn}: ${(bytes / (RATE * 2)).toFixed(2)} seconds received`);
        if (mode === 'echo') {
          phase = 'responding';
          const correction = normalizeEcho ? echoLevelGain(Buffer.concat(echoChunks, bytes)) : 1;
          log(`Turn ${turn}: echo level correction ${correction.toFixed(2)}x, output gain ${gain}`);
          for (const chunk of echoChunks) enqueue(chunk, correction);
          echoChunks = [];
          replyDone = true;
          pump();
        } else {
          phase = 'committing';
          api({ type: 'input_audio_buffer.commit' });
        }
      } else if (message.type === 'ack') {
        const count = message.count ?? 1;
        if (phase !== 'responding' || !Number.isInteger(count) || count < 1 || count > inFlight || message.turn !== turn) return fail('invalid_playback_ack');
        clearTimeout(ackTimer);
        ackTimer = undefined;
        inFlight -= count;
        pump();
      } else if (message.type === 'playback_done') {
        if (phase !== 'responding' || !finishSent || inFlight || message.turn !== turn) return fail('invalid_playback_done');
        const gaps = Number.isInteger(message.buffer_gaps) ? message.buffer_gaps : 'not reported';
        const maxGap = Number.isInteger(message.max_gap_ms) ? message.max_gap_ms : 'not reported';
        log(`Turn ${turn}: playback complete; buffer gaps=${gaps}, max queue wait=${maxGap} ms`);
        idle();
      } else fail('unexpected_control');
    });
    device.on('error', () => fail('device_transport_error'));
    device.on('close', () => {
      phase = 'closed';
      clearTimeout(authTimer);
      clearTimeout(turnTimer);
      clearTimeout(ackTimer);
      upstream?.terminate();
      clearInterval(heartbeat);
      queue = [];
      echoChunks = [];
      if (active === device) active = null;
      if (timerDevice === device) timerDevice = null;
      for (const [id, pending] of pendingTimers) {
        if (pending.device === device) timerReply(id, { ok: false, code: 'device_disconnected', request_id: id, outcome: 'unknown' });
      }
    });
  });
  return server;
}
