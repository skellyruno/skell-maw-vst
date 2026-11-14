// Minimal stand-in for the JUCE subset used by the Constrictor / NightBloom / ButterflyDelay / ShedSkin sources.
#pragma once
#include <vector>
#include <string>
#include <map>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <random>
#include <memory>
#include <atomic>
#include <cassert>
#define JUCE_CALLTYPE
namespace juce {
using uint32 = std::uint32_t;
template <class T> struct MathConstants { static constexpr T pi = (T) 3.14159265358979323846, twoPi = (T) 6.28318530717958647692; };
template <class T> T jmax (T a, T b) { return a > b ? a : b; }
template <class T> T jmax (T a, T b, T c) { return jmax (jmax (a, b), c); }
template <class T> T jmin (T a, T b) { return a < b ? a : b; }
template <class T> T jlimit (T lo, T hi, T v) { return v < lo ? lo : (v > hi ? hi : v); }
inline int roundToInt (float f) { return (int) std::lround (f); }
struct Decibels {
    static float decibelsToGain (float db) { return std::pow (10.0f, db * 0.05f); }
    static float gainToDecibels (float g, float minusInf = -100.0f) { return g > 0 ? jmax (minusInf, 20.0f * std::log10 (g)) : minusInf; }
};
struct Random { std::mt19937 e { 7 }; float nextFloat() { return std::uniform_real_distribution<float> (0, 1) (e); } bool nextBool() { return e() & 1; } };
template <class T> struct SmoothedValue {
    T cur{}, tgt{}, step{}; int n = 0, len = 1;
    void reset (double sr, double sec) { len = jmax (1, (int) (sr * sec)); }
    void setCurrentAndTargetValue (T v) { cur = tgt = v; n = 0; }
    void setTargetValue (T v) { if (v == tgt) return; tgt = v; n = len; step = (tgt - cur) / (T) n; }
    T getNextValue() { if (n > 0) { cur += step; if (--n == 0) cur = tgt; } return cur; }
    T getCurrentValue() const { return cur; }
};
template <class T> struct AudioBuffer {
    std::vector<std::vector<T>> d;
    AudioBuffer (int ch, int n) : d ((size_t) ch, std::vector<T> ((size_t) n, 0)) {}
    int getNumChannels() const { return (int) d.size(); }
    int getNumSamples() const { return (int) d[0].size(); }
    T* getWritePointer (int c) { return d[(size_t) c].data(); }
    const T* getReadPointer (int c) const { return d[(size_t) c].data(); }
    T getSample (int c, int i) const { return d[(size_t) c][(size_t) i]; }
    void applyGainRamp (int s, int n, T g0, T g1) { for (auto& ch : d) for (int i = 0; i < n; ++i) ch[(size_t) (s + i)] *= g0 + (g1 - g0) * (T) i / (T) n; }
};
struct MidiBuffer {};
struct String { std::string s; String() {} String (const char* c) : s (c) {} bool operator== (const String& o) const { return s == o.s; } bool operator< (const String& o) const { return s < o.s; } };
using StringArray = std::vector<String>;
template <class T> struct Point { T x{}, y{}; };
template <class T> struct Rectangle { T x{}, y{}, w{}, h{}; };
struct AudioProcessor { virtual ~AudioProcessor() = default; };
}
