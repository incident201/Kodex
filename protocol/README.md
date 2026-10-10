# Protocol baseline

`codex-0.162.1.json` is generated from the installed Codex CLI 0.162.1:

```sh
codex app-server generate-json-schema --experimental --out /tmp/codex-schema
cp /tmp/codex-schema/codex_app_server_protocol.schemas.json protocol/codex-0.162.1.json
```

The schema contains no local configuration or account data. It is a development
and test reference, not a runtime dependency. Wire messages omit `jsonrpc`.
The local daemon socket and `app-server proxy` use WebSocket framing;
standalone `app-server --stdio` uses JSONL. Do not interchange these transports.

References: [App Server](https://learn.chatgpt.com/docs/app-server),
[OpenAI Codex source](https://github.com/openai/codex) (Apache-2.0).
