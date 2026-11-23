# ESP32 focus timer

The complete, versioned guide is [Focus timer](../docs/wiki/Focus-Timer.md), also published in the [GitHub wiki](https://github.com/mattie02/ai-desk-buddy/wiki/Focus-Timer).

With the backend running, use another terminal in `backend/`:

```sh
npm run timer -- start 25m
npm run timer -- status
```

Success requires a matching ESP32 receipt. An accepted countdown and its local chimes continue through Wi-Fi/backend outages, but not device reset or power loss. Spoken timer commands and typed cancellation are not implemented.
