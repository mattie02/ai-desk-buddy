# Mac voice backend

The Mac bridges ESP32 push-to-talk audio to OpenAI Realtime. API credentials, model, voice, personality, and reply gain stay here. Node.js 22 or newer is required.

See the [setup guide](../docs/wiki/Setup-and-Operation.md) for fresh configuration and firmware upload, and the [architecture](../docs/wiki/Architecture.md) and [protocol](../docs/wiki/Protocol.md) for implementation details.

From this directory, on a fresh checkout:

```sh
npm ci
npm run setup
```

Edit the generated local configuration files, then run:

```sh
npm start
```

Setup starts with echo mode and refuses to overwrite existing configuration. For AI replies, set `OPENAI_API_KEY` locally in `.env`, set `BACKEND_MODE=realtime`, and restart. Never commit `.env` or `include/secrets.h`.

Typed timer commands use a second terminal:

```sh
npm run timer -- start 25m
npm run timer -- status
```

See [timer behavior and offline testing](../docs/wiki/Focus-Timer.md). Spoken timer actions and typed cancellation are not implemented yet.

Run `npm test` for the localhost protocol tests and native firmware clock checks. These do not call OpenAI. The [verification record](../docs/wiki/Verification-and-Troubleshooting.md) distinguishes automated checks from actual hardware acceptance.
