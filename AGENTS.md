# Aevora Agent Instructions

[CLAUDE.md](CLAUDE.md) is the authoritative project guide for every agent, including Codex. Read it in full before any work and follow it. If CLAUDE.md and AGENTS.md ever disagree, CLAUDE.md wins.

- Before changing code, read [README.md](README.md) and [WORK_PLAN.md](docs/Aevora/WORK_PLAN.md).
- Locate files via [CODEBASE_INDEX.md](docs/Aevora/CODEBASE_INDEX.md).
- Never run `git commit`, `git tag`, or `git push`.
- Communicate with the user in Japanese; write code, comments, and project documentation in English.
- A Code reviewer reviews against the "Readable code", "Architecture and scope", "Working method", and "Build and verification rules" sections of CLAUDE.md and names the rule behind each finding.
- An md Implementer edits Markdown only.
- A Code Explorer explores and checks the codebase read-only and reports `path:line` references.
