# Providers and USB protocol

## Normalized snapshot

The Python bridge uses this structure. These values are invented examples:

```json
{
  "provider": "CODEX",
  "plan": "CHATGPT PLAN",
  "status": "live",
  "sampled_at": 1800000000,
  "rows": [
    {"label": "5H", "remaining": 64, "reset": 1800010000, "detail": "-"},
    {"label": "WEEKLY", "remaining": 82, "reset": 1800500000, "detail": "-"}
  ]
}
```

`remaining` is percent left, not percent consumed. Use `null` for unknown, not zero. All times are Unix seconds; reset `0` means unknown. At most three windows are sent. `sampled_at` must retain the actual online sample time; loading a disk cache must not change it.

Statuses: `live`, `sync`, `error`, `auth`, `empty`. Only fresh `live` data can show a percentage. A window with a known reset at or before the current time is masked independently. Samples older than 900 seconds, missing sample times or times more than 30 seconds in the future are not current.

`codex_snapshot` prefers `rateLimitsByLimitId["codex"]` when available, otherwise the legacy `rateLimits` field. Windows come from the service. Remaining percent is `100 - usedPercent`, clamped to 0–100; missing values stay unknown.

## Wire packet

115200 baud, newline-terminated ASCII:

```text
Q1|sequence|sent_at|sampled_at|provider|plan|status|row_count|label|remaining|reset|detail[|...]
```

Each row adds four fields. Unknown remaining is `-1`. `detail` uses `-` as a placeholder; empty fields are not sent. Text is restricted to ASCII and strips delimiters/newlines. Input validation rejects malformed packets without replacing the last accepted data.

Device acknowledgement:

```text
ACK seq=1 provider=CODEX rows=2 state=live
```

The PC estimates source time using `sent_at`; firmware advances it with elapsed `millis()`. Both bridge and firmware enforce freshness. Device restart or sequence mismatch causes a syncing packet and a new online query.

## Commands

| Command | Purpose |
|---|---|
| `STATUS` | Quota, freshness, render counters, sleep state and configured timeout |
| `NEXT` | Cycle the focused window |
| `FREEZE` | Toggle photo mode |
| `REDRAW` | Recompose the framebuffer; identical pixels need no LCD write |
| `DUMP` | Read the RGB565 framebuffer as run-length data; capture helper decodes it |
| `SCREEN OFF` | Diagnostic backlight off |
| `SCREEN ON` | Diagnostic wake through the same wake function as physical touch |
| `IDLE_MS 120000` | Volatile idle timeout in milliseconds; valid range 1000–3600000 |

The idle timeout resets to 120000 on device boot. Quota packets and health/status queries do not reset the touch idle timer. `SCREEN ON` is an explicit diagnostic wake. Ordinary data updates do not wake the screen. Photo mode intentionally preserves its frozen frame when waking.

## Experimental provider adapters

Agent Plan:

```sh
python bridge/monitor.py --source volc
```

Requires authenticated official `arkcli`; calls `arkcli usage plan --product agent-plan --format json`. AFP is kept as the provider's own unit, not relabeled as tokens or currency. The adapter is included but not validated against a live account.

OpenRouter:

Set `D1_OPENROUTER_MANAGEMENT_KEY` locally, then run `python bridge/monitor.py --source openrouter`. It calls `GET /api/v1/credits` and displays `total_credits - total_usage` in USD. A management key is required; ordinary inference keys may not have access. Never place that key in firmware, a source file or an issue. This adapter has not been validated against a live account.
