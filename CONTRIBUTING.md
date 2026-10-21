# Contributing

Issues and pull requests are welcome. Useful contributions include additional quota providers, clearer account-state messages, platform testing and D1 display refinements.

1. Install `requirements.txt` in a virtual environment.
2. Run `python -m unittest discover -s bridge -p "test_*.py"`.
3. For firmware changes, follow [BUILD.md](docs/BUILD.md) and describe the exact hardware validation performed.
4. Keep unknown values distinct from zero, preserve sampling timestamps, and never replay a cached percentage as a fresh online reading.
5. Keep credentials on the computer. Do not include account payloads, tokens, device identity files or unsanitized logs in issues or pull requests. Share synthetic fixtures and fixed diagnostic reasons instead.

Provider adapters return the normalized structure in [PROTOCOL.md](docs/PROTOCOL.md). New fixtures should use invented percentages and timestamps, without requiring a real account or a USB device in CI.

By contributing, you agree to license your original contribution under the project's MIT license. Existing dependency licenses remain in effect.
