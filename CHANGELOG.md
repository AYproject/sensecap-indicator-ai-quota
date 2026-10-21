# Changelog

## 0.5.2 — 2026-10-21, initial public source release

The device implementation was validated on 2026-10-20; the public project was packaged on 2026-10-21.

- Studio Glass layout: graphite, warm white, restrained sage and pre-blurred materials.
- Partial framebuffer updates, without full-screen redraw on each quota update.
- Online Codex quota via the existing ChatGPT account login; additional provider adapters included as experimental.
- Automatic USB reconnect and fresh online lookup after device restart.
- Stale/error/sync states hide percentages; genuine zero remains zero.
- Queries every five minutes; failed reads retry after 15/30/60/120 seconds.
- Two-minute idle screen-off, touch anywhere to wake; the first entire gesture is consumed.
- Quota reception continues with the backlight off; current values are composed before wake.
- Portable startup/build scripts, firmware images, checksums, bilingual documentation and CI.

The public release does not include account snapshots, hardware identifiers, local logs or the original device flash backup.
