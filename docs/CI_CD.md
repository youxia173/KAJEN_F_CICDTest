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
| cpplint | **manual only** (Actions → CI) | `scripts/linter/linter.sh` via `.github/workflows/ci.yml` |
| cppcheck | **manual only** | `scripts/cppcheck/runner.sh` |
| unittest | **manual only** (if test dirs exist) | `scripts/unittest/runner.sh` |
| firmware build | optional (`ENABLE_FIRMWARE_BUILD=true` + GHCR image) | `.github/workflows/ci.yml` → `docker pull` + `docker run build` |
| docker image (GHCR) | **manual only** (Actions → Run workflow) | `.github/workflows/docker-image.yml` |
| release package | tag `v*` | rename `artifact/...-full.s37` → `silabs_MatterAndZigger_SixG301_V{ver}.s37` only |
| Homesmart OTA | release, if secrets set | placeholder step in `release.yml` |

## Local CI

While cloud CI is paused, use local checks:

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

Re-enable auto CI on push/PR: uncomment `push` / `pull_request` in `.github/workflows/ci.yml`.

## Repo variables / secrets

**Variables (Settings → Variables):**

- `ENABLE_FIRMWARE_BUILD` — leave **unset or `false`** until GHCR image build succeeds once; then set `true`
- `GHCR_FIRMWARE_IMAGE` — optional override, default `ghcr.io/<owner>/kajen-sixg301:sdk-2025.12.1`
- `CI_STRICT_LINT=true` — make cpplint fail the CI job

Cloud firmware build uses the **full SiSDK Docker image** on GHCR (see `docker/README.md` § GHCR).  
Run **Build Docker image (GHCR)** manually from Actions while fixing the image; re-enable auto push in `docker-image.yml` when green.  
Only then set `ENABLE_FIRMWARE_BUILD=true`.

**Secrets (Settings → Secrets):**

- `IKEA_OTA_API_URL` — Homesmart OTA endpoint (when provided)
- `IKEA_OTA_TOKEN` — OTA credential

## Still to do (IKEA full compliance)

1. ~~Package Silicon Labs build env as Docker image~~ → GHCR workflow `docker-image.yml` (run once on GitHub)
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
