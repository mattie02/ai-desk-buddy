import { readFileSync } from 'node:fs';
import { createBuddyServer } from './bridge.mjs';

try {
  const server = createBuddyServer({
    host: process.env.HOST || '0.0.0.0',
    port: Number(process.env.PORT || 8765),
    token: process.env.DEVICE_TOKEN,
    apiKey: process.env.OPENAI_API_KEY,
    model: process.env.OPENAI_REALTIME_MODEL || 'gpt-realtime-2.1',
    voice: process.env.OPENAI_VOICE || 'marin',
    gain: Number(process.env.OUTPUT_GAIN || 0.06),
    normalizeEcho: process.env.ECHO_NORMALIZE !== '0',
    mode: process.env.BACKEND_MODE || 'realtime',
    instructions: readFileSync(new URL('personality.txt', import.meta.url), 'utf8'),
  });
  server.on('listening', () => console.log(`Desk Buddy listening on port ${server.address().port} (${process.env.BACKEND_MODE || 'realtime'})`));
  server.on('error', error => { console.error(`Backend listener error: ${error.code || 'unknown'}`); process.exitCode = 1; });
  const stop = () => {
    for (const client of server.clients) client.terminate();
    server.close();
  };
  process.on('SIGINT', stop);
  process.on('SIGTERM', stop);
} catch (error) {
  console.error(error.message);
  process.exitCode = 1;
}
