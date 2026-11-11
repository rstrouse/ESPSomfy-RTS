# Security

## Reporting a vulnerability

Please don't open a public issue. Report it privately through [GitHub's vulnerability reporting](https://github.com/cjkas/ESPSomfy-RTS/security/advisories/new) and give the firmware version, the board, and the steps to reproduce it.

Fixes go into the latest release only, so check that the problem is still there in it.

The Home Assistant integration has [its own repo](https://github.com/cjkas/ESPSomfy-RTS-HA) and its own reporting page.

## What the device protects against

The device is meant for a local network. In the Secured mode the socket and every HTTP route need a login, except the few the login page itself needs (the web UI's files, `/loginContext`, `/login`, `/upnp.xml`). The threat model, what is in scope and what isn't, is in [docs/SECURITY_PLAN.md](docs/SECURITY_PLAN.md) and [ADR 0006](docs/adr/0006-two-security-modes-eleven-http-routes.md). Don't expose the device to the internet.

## Checking a download

Each release file (`.bin`, `.zip`) comes with a signed attestation that this repo's [build workflow](.github/workflows/build.yaml) built it from the release's commit. Check one with the [GitHub CLI](https://cli.github.com/):

```
gh attestation verify SomfyController.ino.esp32.bin --repo cjkas/ESPSomfy-RTS
```

Or compare the checksums with the release's `SHA256SUMS` file:

```
sha256sum --check --ignore-missing SHA256SUMS
```

Releases up to 4.0.1 have neither.

## How the build is kept clean

- Every action in the workflows is pinned to a commit, the firmware's libraries and the build's Python tools to exact versions. Dependabot proposes action updates after a week's cooldown.
- The workflows can only read the repo's contents. Only the release job can change them (the release's files, the version bump), and it runs no build tools. The scanning jobs can also report to the Security tab, and the coverage job publishes to GitHub Pages.
- [CodeQL](.github/workflows/codeql.yaml) scans the C++, the web UI, the Python scripts and the workflows on every PR. [zizmor](.github/workflows/zizmor.yaml) lints the workflows, and [OpenSSF Scorecard](.github/workflows/scorecard.yaml) rates the repo's practices. Findings are on the repo's Security tab.
