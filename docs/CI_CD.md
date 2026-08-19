# CI/CD (IKEA NonFuncReq)

This repository follows Inter IKEA Homesmart DevOps expectations:

- Source on GitHub, trunk-based short-lived branches
- CI via GitHub Actions (pipeline YAML in repo)
- Build logic lives in `scripts/` (pipeline steps stay short)
- Local reproduction of CI: `bash scripts/ci_local.sh`
- Release tags `vX.Y.Z`; unsigned zip + release notes on GitHub Releases
- Secrets (OTA credentials) in GitHub Secrets — never in source
- Firmware build env: full SiSDK image on GHCR (`kajen-sixg301:sdk-2025.12.1`)

## Current status (2026-08)

| Capability | Status |
|---|---|
| GHCR full firmware image | **Done** (manual Build Docker image workflow) |
| Cloud firmware compile in CI | **Done** (`ENABLE_FIRMWARE_BUILD=true`) |
| Sequential release pipeline | **Done** (CodeAnalysis → UnitTest → Build → Release) |
| Tag → unsigned zip + GitHub Release | **Done** (`{project_id}-{version}-unsigned.zip`) |
| Push/PR auto CI | **Off on purpose** (save private-repo Actions minutes) |
| Homesmart OTA API | **Pending** IKEA credentials / API |
| Mirror to Inter IKEA GitHub | **Pending** delivery |

## Pipeline (sequential)

Tag push `v*` runs **all four stages** in one workflow (not parallel, not release-only):

```
code-analysis  →  unit-test  →  firmware-build  →  release-package
     (1)              (2)            (3)                 (4)
```

Implementation: `.github/workflows/release_template.yaml` (reused by Release + manual CI).

| Stage | Script / action |
|---|---|
| 1 Code analysis | `scripts/linter/*`, `scripts/cppcheck/*` → `code_quality_report.tar.gz` |
| 2 Unit test | `scripts/unittest/runner.sh` (skipped if no test dirs) |
| 3 Build | GHCR `docker run … build` |
| 4 Release | `scripts/release/package_release.sh` → GitHub Release |

**Manual CI** (Actions → **CI**): runs stages 1–3 only (no Release). Needs `ENABLE_FIRMWARE_BUILD=true` for stage 3.

**Release** (push tag `v*`): runs stages 1–4. Does **not** depend on a separate prior CI run.

## Release package layout

Configured in `scripts/release/project.env`:

```bash
IKEA_PROJECT_ID="4476-20480"   # placeholder until IKEA assigns final ID
```

Output zip: **`4476-20480-0.3.6-unsigned.zip`** (example for tag `v0.3.6`).

| File inside zip | Purpose |
|---|---|
| `Matter-Bootloader_113W.s37` | Bootloader — flash separately |
| `ZigbeeMatterLight_113W.s37` | Application — flash separately |
| `ZigbeeMatterLightSolution_SixG301M113W-full.s37` | Optional combined image (if build produced it) |

OTA artifacts are **not** included yet (tutorial TBD).

Legacy single-file naming (`silabs_MatterAndZigger_SixG301_V*.s37`) is kept in `scripts/release/package_firmware.sh` for local convenience only.

## Recommended flow (public test repo first)

1. Set repo variable **`ENABLE_FIRMWARE_BUILD=true`**
2. Ensure GHCR image exists (`Build Docker image (GHCR)` workflow or `GHCR_FIRMWARE_IMAGE`)
3. **Manual QA** (optional): Actions → **CI** → Run workflow
4. **Release** (only bump forward):

```bash
git checkout main && git pull
git tag v0.3.7
git push origin v0.3.7
```

5. Check GitHub Release for `{project_id}-{version}-unsigned.zip`

## Local CI

```bash
bash scripts/ci_local.sh              # lint + cppcheck
bash scripts/ci_local.sh --strict
bash scripts/ci_local.sh --with-build # needs local SiSDK / Studio
bash scripts/release/package_release.sh 0.3.6 release_out
```

## Repo variables / secrets

**Variables (Settings → Variables):**

- `ENABLE_FIRMWARE_BUILD=true` — run firmware job in manual CI
- `GHCR_FIRMWARE_IMAGE` — optional; default `ghcr.io/<owner>/kajen-sixg301:sdk-2025.12.1`
- `CI_STRICT_LINT=true` — make cpplint fail code-analysis
- `ENABLE_OTA_UPLOAD=true` — only after OTA secrets + real API are ready

**Secrets (Settings → Secrets):**

- `IKEA_OTA_API_URL` — Homesmart OTA endpoint (when provided)
- `IKEA_OTA_TOKEN` — OTA credential

## Workflows

| Workflow | Trigger | Stages |
|---|---|---|
| `ci.yml` | Manual | 1 → 2 → 3 |
| `release.yml` | Tag `v*` / manual | 1 → 2 → 3 → 4 (4 only on tag or manual + create_release) |
| `release_template.yaml` | `workflow_call` | Shared template |
| `docker-image.yml` | Manual | Build/push GHCR image |

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
- `scripts/release/package_release.sh` (Release zip), `scripts/release/package_firmware.sh` (legacy single .s37)
- `scripts/flash.sh` (device flash; not used in cloud CI)

`scripts/ci_local.sh` is the shared entry for local checks.
