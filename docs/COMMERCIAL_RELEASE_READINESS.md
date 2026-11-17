# Skell Maw — Commercial Release Readiness

**Snapshot:** 2026-11-17  
**Status:** **PREVIEW ONLY — NOT APPROVED FOR COMMERCIAL DISTRIBUTION**  
**Scope:** Seven-plugin Windows x64 VST3 collection. This document records evidence and open gates; it does not select legal terms or authorize a release.

## Exact tested preview baseline

- Packaging PR: [#3 — Validate and package Skell Maw Windows VST3 bundles](https://github.com/skellyruno/skell-maw-vst/pull/3) (draft).
- Tested PR source head: `13a92aacb7b8c3538f12b8615bdfa86d9c68710a`.
- Exact-head workflow: [run #44031640565](https://github.com/skellyruno/skell-maw-vst/actions/runs/44031640565) — completed successfully.
- The workflow checked out the PR merge ref `refs/pull/3/merge`, merge commit `597afe2a63ed8d78ffe1f511e51a2c0f9edd5eda`; the PR source head remains the SHA above.
- Results from that run:
  - JUCE-free DSP regression tests: **4/4 passed**.
  - Windows x64 Release VST3 build: **passed**.
  - Package-verifier unit tests: **9/9 passed**, including the existing-output preservation regression.
  - Seven expected VST3 bundles staged and structurally validated.
  - pluginval strictness 5: **7/7 plugins passed**.
  - Artifact upload: **passed**.

### Preserved artifact

- [SkellMaw-Windows-VST3 artifact #14306008513](https://github.com/skellyruno/skell-maw-vst/actions/runs/44031640565/artifacts/14306008513)
- Size: 60,106,306 bytes.
- GitHub artifact digest: `sha256:efe0d67b4c60a2a7fd2e5e0ec169f5b8359b2f00bc58a441bcb28bb9d46e582f`.
- Downloaded independent copy: `SkellMaw-Windows-VST3-run44031640565.zip`; local SHA-256 matched the GitHub digest.
- Independent inspection: ZIP CRC passed; 17 entries; all 16 `SHA256SUMS.txt` entries recomputed correctly; exactly seven plugin bundles; all seven modules are AMD64 PE32+ (`0x8664 / 0x020b)); each `moduleinfo.json` parsed as strict JSON and contained the expected two classes.
- The package's own `build-info.json` marks it `preview-only; not a commercial release`. The artifact is retained by GitHub until **2026-12-17 06:49 UTC**; keep the downloaded copy as the durable preview baseline.

Earlier independent preview baseline (do not overwrite): [artifact #14295408141](https://github.com/skellyruno/skell-maw-vst/actions/runs/44007318584/artifacts/14295408141), SHA-256 `97e9e42da8969ea68ff345d4b875d1905a0ea7f8ed49b82760b3ff79b7aed4ef`.

## Release gate ledger

| Gate | Current evidence | Status |
|---|---|---|
| Windows x64 build | Exact-head workflow #44031640565 | **PASS — preview build** |
| DSP regression suite | 4/4 JUCE-free CTest cases | **PASS — not an audio A/B test** |
| Package regression suite | 9/9 Python unit tests | **PASS** |
| VST3 structure / architecture | Seven AMD64 PE32+ modules; strict metadata parse; 16 checksums verified | **PASS — package integrity only** |
| Host validator | pluginval strictness 5, 7/7 | **PASS — not FL Studio acceptance** |
| Source integration | PR #3 remains draft and is stacked on PR #1's source branch; PR #1 and UI PR #2 remain separate open drafts | **BLOCKED — integration/review required** |
| License and third-party attribution | No root `LICENSE`, `LICENSE.txt`, or `COPYING` at tested head; README explicitly calls out a VST3 SDK licence check. No complete dependency/attribution audit is recorded here. | **BLOCKED — owner/legal decision** |
| Vendor identity | Package verifier reports blank `Factory Info.URL` and `Factory Info.E-Mail` as release warnings; the current CI permits preview packaging with those fields blank. | **BLOCKED — owner must choose official URL and support contact** |
| Windows installer / signing | Current artifact is a raw VST3 preview package with `PREVIEW-ONLY.txt`; no installer or signing evidence is present. | **NOT IMPLEMENTED / NOT VERIFIED** |
| FL Studio acceptance | No verified scan/load, project reopen, preset/state recall, or automation round-trip on the target DAW. | **UNVERIFIED** |
| Real audio quality and performance | No controlled listening A/B or measured target-hardware CPU/latency evidence in this run. | **UNVERIFIED** |
| UI visual and interaction QA | Repository `AGENTS.md` says local Quick4 is not yet verified. The hero-readout bounds fix is isolated in draft PR #2 and is not part of the tested PR #3 head. No current Maw screenshots were captured in this run. | **UNVERIFIED** |
| SkellyHub distribution / rollback | No verified install, update, license delivery, or rollback transaction for this collection. | **UNVERIFIED** |

## Decisions required before a commercial candidate

1. Select the intended source/distribution license and review JUCE 8.0.12, the VST3 SDK terms, and all bundled third-party notices. **Do not add a license file or choose terms by automation.**
2. Supply the official Skell Audio vendor URL and support email, then make missing release metadata a failing release-mode check while keeping preview mode usable.
3. Decide the Windows installer/signing approach and how versioned updates, backups, uninstall, and rollback will work through SkellyHub.
4. Complete real FL Studio scan/load and saved-project, preset, parameter-state, and automation recall checks for all seven plugins.
5. Perform controlled audio A/B listening and record CPU/latency on representative target hardware.
6. Wire and run the repository's actual JUCE Quick4 renderer; capture and inspect default, menu/control, active, and supported alternate scenes, including compact sizing. Keep screenshots separate from build evidence.
7. Integrate the stacked source/package PRs only after their dependency chain is reviewed; retarget the packaging branch to `main` and rerun exact-head Windows checks after integration.
8. Create a versioned release candidate and changelog only after the gates above are accepted.

The README also notes code-signing/notarization for a future macOS distribution. The artifact validated here is Windows x64 only; it is not evidence of a macOS build.

## Preservation / change boundary

- This readiness document is documentation-only. It does not change DSP, UI, artwork, parameter IDs, presets, plugin state, or CMake packaging behavior.
- The existing Actions artifacts and the downloaded exact-head ZIP remain separate, immutable baselines.
- No installed plugin, FL Studio project, automation, preset, or production binary was overwritten.
- No license terms, vendor contact details, signing identity, payment flow, or commercial release decision were invented.
