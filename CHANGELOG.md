# Skell Maw changelog

## 1.0.0 — source-quality update (2026-11-14, not a released binary)

- Night Bloom: smooth Bloom/Mix and Output automation over 10 ms, initialized from restored parameter values to avoid an unwanted preset-load fade. Automation progression remains identical on both audio channels.
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
