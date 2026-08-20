# CI/CD (IKEA NonFuncReq)

This repository follows Inter IKEA Homesmart DevOps expectations:

- Source on GitHub, trunk-based short-lived branches
- CI via GitHub Actions (pipeline YAML in repo)
- Build logic lives in `scripts/` (pipeline steps stay short)
- Local reproduction of CI: `bash scripts/ci_local.sh`
- Release tags `vM.m.p` (IKEA semver); unsigned zip + `config.json` on GitHub Releases
- Secrets (OTA credentials) in GitHub Secrets — never in source
- Firmware build env: full SiSDK image on GHCR (`kajen-sixg301:sdk-2025.12.1`)

## Current status (2026-08)

| Capability | Status |
|---|---|
| GHCR full firmware image | **Done** (manual Build Docker image workflow) |
| Cloud firmware compile in CI | **Done** (`ENABLE_FIRMWARE_BUILD=true`) |
| Sequential release pipeline | **Done** (CodeAnalysis → UnitTest → Build → Release) |
| Tag → unsigned zip + GitHub Release | **Done** (`{project_id}-{version}-unsigned.zip`) |
| Host unit tests (cmocka + gcovr) | **Started** (`kt_components/kajen_level_util`) |
| Push/PR auto CI | **On** (public test repo; push to main) |
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
| 2 Unit test | `kt_components/*/test` + cmocka + gcovr → `unit_test_report.tar.gz` |
| 3 Build | GHCR `docker run … build` |
| 4 Release | `scripts/release/package_release.sh` → GitHub Release |

**Manual CI** (Actions → **CI**): runs stages 1–3 only. Also runs automatically on **push/PR to main**.

**Release** (push tag `v*`): runs stages 1–4. Does **not** depend on a separate prior CI run.

GHCR image lookup order for firmware-build: `GHCR_FIRMWARE_IMAGE` variable → `ghcr.io/<owner>/kajen-sixg301:sdk-2025.12.1` → `ghcr.io/barryjim/kajen-sixg301:sdk-2025.12.1`.

## Release package layout

Configured in `scripts/release/project.env`:

```bash
IKEA_PROJECT_ID="4476-36900"   # VID-PID decimal (4476=0x117C, 36900=0x9024 KAJEN floor)
IKEA_PRODUCT_ID="0x9024"       # config.json productId
IKEA_OTA_MIN_VERSION="0x01010000"
OTA_VENDOR_ID="0xFFF1"         # Matter .ota VID (test default; match CHIPProjectConfig)
OTA_PRODUCT_ID="0x8005"        # Matter .ota PID (test default)
OTA_GBL_CMD="gbl4"             # MG301; use gbl for MG24
```

Output zip: **`4476-36900-1.1.0-unsigned.zip`** (example for tag `v1.1.0`).

| File inside zip | Purpose |
|---|---|
| `bootloader.s37` | Bootloader |
| `firmware.s37` | Application firmware |
| `firmware.ota` | Matter OTA (`commander gbl4` + `ota create`) |
| `config.json` | OTA metadata (`productId`, `version`, `minVersion`, `maxVersion`) |

Version encoding: **`0xMMmmPPBB`** — e.g. `0x01010001` = v1.1.0 build 1. Build byte increments on each OTA drop; bump `sl_matter_config.h` before tagging.

OTA creation (SiLabs [Matter OTA](https://docs.silabs.com/matter/2.8.1/matter-ota/02-ota-software-update)):

```bash
commander gbl4 create firmware.gbl --data firmware.s37 --compress lzma
commander ota create --type matter --input firmware.gbl \
  --vendorid 0xFFF1 --productid 0x8005 \
  --swstring "1.1.0" --swversion 0x01010001 --digest sha256 -o firmware.ota
```

Release packaging runs inside the GHCR firmware image so Commander is available. When firmware VID/PID moves to production (`0x117C` / `0x9024`), update `OTA_VENDOR_ID` / `OTA_PRODUCT_ID` in `project.env` to match.

Legacy single-file naming (`silabs_MatterAndZigger_SixG301_V*.s37`) is kept in `scripts/release/package_firmware.sh` for local convenience only.

## Recommended flow (public test repo first)

1. Set repo variable **`ENABLE_FIRMWARE_BUILD=true`**
2. Ensure GHCR image exists (`Build Docker image (GHCR)` workflow or `GHCR_FIRMWARE_IMAGE`)
3. **Manual QA** (optional): Actions → **CI** → Run workflow
4. **Release** (only bump forward):

```bash
git checkout main && git pull
git tag v1.1.0
git push origin v1.1.0
```

5. Check GitHub Release for `{project_id}-{version}-unsigned.zip`

## Local CI

```bash
bash scripts/ci_local.sh              # lint + cppcheck
bash scripts/ci_local.sh --strict
bash scripts/ci_local.sh --with-build # needs local SiSDK / Studio
bash scripts/ci_local.sh --with-unittest # cmocka tests under kt_components/
bash scripts/unittest/runner.sh          # unit tests only
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
| `ci.yml` | Push/PR main, manual | 1 → 2 → 3 |
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
- `scripts/release/package_release.sh` (Release zip + `firmware.ota`), `scripts/release/create_ota.sh` (gbl4/ota), `scripts/release/package_firmware.sh` (legacy single .s37)
- `scripts/flash.sh` (device flash; not used in cloud CI)

`scripts/ci_local.sh` is the shared entry for local checks.
