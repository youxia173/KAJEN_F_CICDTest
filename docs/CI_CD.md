# CI/CD (IKEA NonFuncReq)

This repository follows Inter IKEA Homesmart DevOps expectations:

- Source on GitHub, trunk-based short-lived branches
- CI via GitHub Actions (pipeline YAML in repo)
- Build logic lives in `scripts/` (pipeline steps stay short)
- Local reproduction of CI: `bash scripts/ci_local.sh`
- Release tags `vX.Y.Z`; artifacts + release notes on GitHub Releases
- Secrets (OTA credentials) in GitHub Secrets — never in source
- Firmware build tooling should move into Docker / custom Actions when SiSDK is packaged

## What runs today

| Stage | Trigger | Script / workflow |
|---|---|---|
| cpplint | PR / push | `scripts/linter/linter.sh` via `.github/workflows/ci.yml` |
| cppcheck | PR / push | `scripts/cppcheck/runner.sh` |
| unittest | PR / push (only if `kt_components/` or `unit_test/` exist) | `scripts/unittest/runner.sh` |
| firmware build | optional (`ENABLE_FIRMWARE_BUILD=true` + self-hosted SiLabs runner) | `scripts/ci_local.sh --build-only` |
| release package | tag `v*` | rename `artifact/...-full.s37` → `silabs_MatterAndZigger_SixG301_V{ver}.s37` only |
| Homesmart OTA | release, if secrets set | placeholder step in `release.yml` |

## Local CI

```bash
# lint + cppcheck (same as default GitHub CI)
bash scripts/ci_local.sh

# fail on lint findings
bash scripts/ci_local.sh --strict

# also build firmware (needs Simplicity Studio / slt on this machine)
bash scripts/ci_local.sh --with-build

# firmware only (no lint/cppcheck)
bash scripts/ci_local.sh --build-only
```

## Repo variables / secrets

**Variables (Settings → Variables):**

- `ENABLE_FIRMWARE_BUILD=true` — enable firmware job
- `SILABS_RUNNER` — self-hosted runner label/name that has SiSDK + ARM GCC (`slt`, ninja, commander)
- `CI_STRICT_LINT=true` — make cpplint fail the CI job

GitHub-hosted `ubuntu-latest` **cannot** compile this firmware: CMake looks up ninja/gcc via Silicon Labs `slt` under `~/.silabs/slt/...`. Without a self-hosted runner, the firmware job will skip instead of failing.

**Secrets (Settings → Secrets):**

- `IKEA_OTA_API_URL` — Homesmart OTA endpoint (when provided)
- `IKEA_OTA_TOKEN` — OTA credential

## Still to do (IKEA full compliance)

1. Package Silicon Labs build env as Docker image → custom GitHub Action (see `docker/README.md`; local slim image first)
2. Mirror this repo to Inter IKEA GitHub on delivery
3. Wire official Homesmart OTA upload API in `release.yml`
4. Add host unit tests under app `*/test/` + `kt_components` if required
5. IaC for any self-hosted runners (Terraform/Ansible) so IKEA can reproduce infra

## Docker (firmware build env)

See `docker/README.md`. Adapted from colleague Li-Bat image for this repo (`hans` HOME, SiSDK 2025.12.1, SIMG301).

```bash
# After installing Docker Engine:
INSTALL_SLT_PACKAGES=0 IMAGE_TAG=kajen-sixg301:slim ./docker/build-image.sh
docker run --rm -v "$PWD":/workspace -v "$HOME/.silabs":/home/hans/.silabs:ro \
  -w /workspace kajen-sixg301:slim build
```

## Mapping to existing project scripts

IKEA asked to integrate existing SKEPPSKLOCKA-style scripts into Actions. This tree already has:

- `scripts/linter`, `scripts/cppcheck`, `scripts/codeanalysis`
- `scripts/unittest`, `scripts/format`
- `scripts/fw_packaging`, `scripts/release`
- `scripts/flash.sh` (device flash; not used in cloud CI)

`scripts/ci_local.sh` is the single entry that Actions and developers share.
