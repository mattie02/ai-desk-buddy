import { randomBytes } from 'node:crypto';
import { existsSync, readFileSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { networkInterfaces } from 'node:os';

const envPath = new URL('.env', import.meta.url);
const secretPath = new URL('../include/secrets.h', import.meta.url);
if (existsSync(envPath) || existsSync(secretPath)) {
  console.error('Setup stopped: .env or secrets.h already exists. Preserve those settings and edit them locally.');
  process.exitCode = 1;
} else {
  const token = randomBytes(24).toString('hex');
  const env = readFileSync(new URL('.env.example', import.meta.url), 'utf8')
    .replace('DEVICE_TOKEN=', `DEVICE_TOKEN=${token}`);
  const interfaces = networkInterfaces();
  const candidates = (interfaces.en0 || Object.values(interfaces).flat())
    .filter(entry => entry.family === 'IPv4' && !entry.internal);
  const address = candidates.length === 1 ? candidates[0].address : '';
  const secret = readFileSync(new URL('../include/secrets.example.h', import.meta.url), 'utf8')
    .replace('#define BUDDY_DEVICE_TOKEN ""', `#define BUDDY_DEVICE_TOKEN "${token}"`)
    .replace('#define BUDDY_SERVER_HOST ""', `#define BUDDY_SERVER_HOST "${address}"`);
  writeFileSync(envPath, env, { mode: 0o600, flag: 'wx' });
  writeFileSync(secretPath, secret, { mode: 0o600, flag: 'wx' });
  console.log(`Created ${fileURLToPath(envPath)} and ${fileURLToPath(secretPath)} with matching device tokens.`);
  console.log('Check Mac IP in secrets.h. Configure Wi-Fi using the OLED phone setup, or fill the optional Wi-Fi fallback fields locally. Start with BACKEND_MODE=echo in .env; add your API key locally for realtime mode.');
}
