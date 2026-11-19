# Skell Maw changelog

## 1.0.0 — source-quality update (2026-11-14, not a released binary)

- Add strict RFC 8259 metadata parsing: reject duplicate JSON keys, NaN/Infinity, non-object Factory Info, and non-string class categories before packaging.
- Add a manual-only commercial-candidate gate requiring an owner-supplied approved manifest, matching compiled vendor identity, HTTPS vendor URL, support email, a hash-verified LICENSE, and hash-verified third-party notices.
- Keep `build-info.json.release_status` preview-only for commercial candidates so the existing private installer remains compatible; add a Windows PowerShell regression for that path.
- Upgrade checkout/setup-python/upload-artifact Actions to v6 to avoid Node.js 20 deprecation warnings in the Windows release workflow.
- Commercial candidate metadata is written only into the staged package; the source build bundle is not modified. The package remains private, unsigned and unpublished.
- Preserve the default private-preview path and its existing installer/update/rollback workflow. CI continues to create preview packages unless explicitly invoked with the commercial gate.
- Add regression coverage for duplicate/non-finite metadata, malformed Factory Info, missing or pending approval, vendor/contact mismatch, tampered license files, duplicate approval keys, and legal-file checksums.

- Harden rollback manifests: reject reparse-point manifests/backups, machine-wide Program Files destinations, and backup paths that overlap the VST3 scan directory.
- Night Bloom: smooth Bloom/Mix and Output automation over 10 ms, initialized from restored parameter values to avoid an unwanted preset-load fade. Automation progression remains identical on both audio channels.
- Night Bloom: also smooth Above-frequency and Drive automation over 10 ms; refresh high-pass coefficients every eight samples to avoid block-boundary zippering without per-sample trigonometry.
- Add paired-reference regression checks for the first 10 samples after frequency and Drive automation steps.
- Butterfly Delay: cap flutter depth to 45% of short delay times to avoid modulation requesting negative/zero-delay reads.
- Dark Forest: reserve at least one wet-buffer sample even if a host reports a zero maximum block size. Full JUCE/host verification is pending.
- Add a `MAW_DSP_TESTS_ONLY=ON` CMake/CTest route to compile real DSP sources against the existing JUCE shim without network access.
- Extend Night Bloom tests with restored-zero-state and automated Bloom/Output transitions.
- Correct test-shim compiler warnings and run all four DSP suites with strict GCC warnings and UBSan.
- Configure GitHub Actions for PR builds and lightweight DSP checks; cancel superseded **PR** runs only, preserving manual and main runs.
- Preserve plugin names, parameter IDs, defaults, saved-state formats and all supplied art/animation assets.

## Verification

- Linux GCC 14: four DSP test targets compiled cleanly using `-Wall -Wextra -Wpedantic -Werror` and all checks passed.
- Linux CMake/CTest Release: four tests passed.
- Linux UndefinedBehaviorSanitizer: four tests passed.
- **Not verified:** a JUCE-based build (JUCE download unavailable offline), Windows VST3 binaries, FL Studio loading/state recall, UI/animation screenshots, objective CPU/latency, live audio A/B.
