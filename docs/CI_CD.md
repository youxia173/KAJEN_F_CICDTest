# CI/CD (IKEA NonFuncReq)

This repository follows Inter IKEA Homesmart DevOps expectations:

- Source on GitHub, trunk-based short-lived branches
- CI via GitHub Actions (pipeline YAML in repo)
- Build logic lives in `scripts/` (pipeline steps stay short)
- Local reproduction of CI: `bash scripts/ci_local.sh`
- Release tags `vX.Y.Z`; artifacts + release notes on GitHub Releases
- Secrets (OTA credentials) in GitHub Secrets — never in source
- Firmware build env: full SiSDK image on GHCR (`kajen-sixg301:sdk-2025.12.1`)

## Current status (2026-08)

| Capability | Status |
|---|---|
| GHCR full firmware image | **Done** (manual Build Docker image workflow) |
| Cloud firmware compile in CI | **Done** (`ENABLE_FIRMWARE_BUILD=true`) |
| Tag → GitHub Release + versioned `.s37` | **Done** |
| Push/PR auto CI | **Off on purpose** (save private-repo Actions minutes) |
| Homesmart OTA API | **Pending** IKEA credentials / API |
| Mirror to Inter IKEA GitHub | **Pending** delivery |

## What runs today

| Stage | Trigger | Script / workflow |
|---|---|---|
| cpplint | **manual** Actions → CI | `scripts/linter/linter.sh` |
| cppcheck | **manual** | `scripts/cppcheck/runner.sh` |
| unittest | **manual** (if test dirs exist) | `scripts/unittest/runner.sh` |
| firmware build | **manual** CI when `ENABLE_FIRMWARE_BUILD=true` | GHCR `docker run … build` |
| docker image (GHCR) | **manual** Actions → Build Docker image (GHCR) | `.github/workflows/docker-image.yml` |
| release package | push tag `v*` (or workflow_dispatch) | package + GitHub Release |
| Homesmart OTA | release + `ENABLE_OTA_UPLOAD=true` | placeholder until API ready |

Auto `push` / `pull_request` triggers in `ci.yml` and `docker-image.yml` stay **commented out** to conserve free Actions minutes. Re-enable later when quota allows.

## Recommended manual flow

1. **Quality + cloud build** (when you need it): Actions → **CI** → Run workflow  
   - Needs variable `ENABLE_FIRMWARE_BUILD=true`  
   - Uploads artifact `firmware-<sha>` (`.s37` / `.gbl`)
2. **Release**: only bump forward (e.g. after `v0.3.5` use **`v0.3.6`**, never re-tag older numbers)

```bash
git checkout main && git pull
git tag v0.3.6
git -c http.proxy= -c https.proxy= push origin v0.3.6
```

3. Release prefers the **CI-built** `firmware-<commit>` artifact for that tag’s commit; if missing, falls back to `artifact/...-full.s37` in the repo.

## Local CI

```bash
bash scripts/ci_local.sh              # lint + cppcheck
bash scripts/ci_local.sh --strict
bash scripts/ci_local.sh --with-build # needs local SiSDK / Studio
bash scripts/ci_local.sh --build-only
```

## Repo variables / secrets

**Variables (Settings → Variables):**

- `ENABLE_FIRMWARE_BUILD=true` — run firmware job in manual CI (GHCR image already available)
- `GHCR_FIRMWARE_IMAGE` — optional; default `ghcr.io/<owner>/kajen-sixg301:sdk-2025.12.1`
- `CI_STRICT_LINT=true` — make cpplint fail the job
- `ENABLE_OTA_UPLOAD=true` — only after OTA secrets + real API are ready

**Secrets (Settings → Secrets):**

- `IKEA_OTA_API_URL` — Homesmart OTA endpoint (when provided)
- `IKEA_OTA_TOKEN` — OTA credential

## Still to do (IKEA full compliance)

1. ~~Package Silicon Labs build env as Docker / GHCR~~
2. ~~Cloud firmware CI via GHCR~~
3. Mirror this repo to Inter IKEA GitHub on delivery
4. Wire official Homesmart OTA upload API in `release.yml`
5. Add host unit tests under app `*/test/` + `kt_components` if required
6. IaC for any self-hosted runners (Terraform/Ansible) if IKEA requires them
7. (Optional) Re-enable auto CI / GHCR on push when Actions minutes allow

## Docker (firmware build env)

See `docker/README.md`.

```bash
INSTALL_SLT_PACKAGES=0 IMAGE_TAG=kajen-sixg301:slim ./docker/build-image.sh
docker run --rm --entrypoint /workspace/docker/entrypoint.sh \
  -v "$PWD":/workspace -v "$HOME/.silabs":/home/hans/.silabs:ro \
  -w /workspace kajen-sixg301:slim build
```

## Mapping to existing project scripts

- `scripts/linter`, `scripts/cppcheck`, `scripts/codeanalysis`
- `scripts/unittest`, `scripts/format`
- `scripts/fw_packaging`, `scripts/release`
- `scripts/flash.sh` (device flash; not used in cloud CI)

`scripts/ci_local.sh` is the shared entry for local checks.
