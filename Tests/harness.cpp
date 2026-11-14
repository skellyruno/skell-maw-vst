#include "MawProcessorBase.h"
#include <cstdio>
#include <cstring>
juce::AudioProcessor* createPluginFilter();
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { ++fails; std::printf ("  FAIL: " __VA_ARGS__); std::printf ("\n"); } } while (0)
static const double SR = 44100.0;
struct Rig {
    std::unique_ptr<juce::AudioProcessor> own; MawProcessorBase* p;
    Rig (int block = 512) { own.reset (createPluginFilter()); p = dynamic_cast<MawProcessorBase*> (own.get()); p->prepareToPlay (SR, block); }
};
// runs a generated signal through in blocks; returns output channel 0/1
static std::vector<std::vector<float>> run (Rig& r, int total, int block, float (*gen)(int, int, float), float amp)
{
    std::vector<std::vector<float>> out (2, std::vector<float> ((size_t) total));
    for (int s = 0; s < total; s += block) {
        int n = std::min (block, total - s);
        juce::AudioBuffer<float> b (2, n);
        for (int c = 0; c < 2; ++c) for (int i = 0; i < n; ++i) b.getWritePointer (c)[i] = gen (c, s + i, amp);
        juce::MidiBuffer m; r.p->processBlock (b, m);
        for (int c = 0; c < 2; ++c) for (int i = 0; i < n; ++i) out[(size_t) c][(size_t) (s + i)] = b.getReadPointer (c)[i];
    }
    return out;
}
static float sine (int, int i, float a) { return a * std::sin (6.2831853f * 1000.0f * (float) i / (float) SR); }
static float noise (int c, int i, float a) { static std::mt19937 e (3); (void) c; (void) i; return a * (std::uniform_real_distribution<float> (-1, 1) (e)); }
static float impulse (int c, int i, float a) { return (c == 0 && i == 0) ? a : 0.0f; }
static float peak (const std::vector<float>& v, size_t from = 0) { float m = 0; for (size_t i = from; i < v.size(); ++i) m = std::max (m, std::abs (v[i])); return m; }
static bool finite (const std::vector<std::vector<float>>& o) { for (auto& c : o) for (float x : c) if (! std::isfinite (x)) return false; return true; }
static float mean (const std::vector<float>& v, size_t from) { double s = 0; for (size_t i = from; i < v.size(); ++i) s += v[i]; return (float) (s / (double) (v.size() - from)); }
static float maxStep (const std::vector<float>& v, size_t from) { float m = 0; for (size_t i = from + 1; i < v.size(); ++i) m = std::max (m, std::abs (v[i] - v[i - 1])); return m; }
static float db (float x) { return 20.0f * std::log10 (x); }

static void testConstrictor()
{
    { Rig r; auto o = run (r, 44100, 512, sine, 0.5f);   // -6 dBFS, thr -18, ratio 4 => ~9 dB GR => ~0.178
      float pk = peak (o[0], 30000); std::printf ("  steady -6dBFS in -> %.3f (%.1f dBFS), expect ~-15\n", pk, db (pk));
      CHECK (std::abs (db (pk) + 14.6f) < 1.5f, "gain reduction off"); CHECK (finite (o), "nan"); }
    for (int knot = 0; knot < 3; ++knot) {
      Rig r; r.p->set ("makeup", 24); r.p->set ("threshold", 0); r.p->set ("knot", (float) knot);
      auto o = run (r, 20000, 333, noise, 1.0f); float pk = peak (o[0]);
      std::printf ("  knot %d, +24dB makeup, full-scale noise -> peak %.3f\n", knot, pk);
      CHECK (finite (o), "nan"); if (knot > 0) CHECK (pk <= 0.9701f, "ceiling exceeded %.3f", pk); }
    { Rig r; r.p->set ("knot", 1); r.p->set ("threshold", 0); auto o = run (r, 8000, 512, sine, 0.5f); float pk = peak (o[0], 4000);
      std::printf ("  soft knot, uncompressed 0.5 sine -> %.4f (transparent expected)\n", pk); CHECK (std::abs (pk - 0.5f) < 0.01f, "soft ceiling colours signal below 0.6"); }
    { Rig r; r.p->set ("attack", 0.1f); r.p->set ("release", 10); r.p->set ("ratio", 20); auto o = run (r, 30000, 64, noise, 1.0f); CHECK (finite (o), "nan at extreme settings"); }
}
static void testNightBloom()
{
    // Settings restored before prepare should be active immediately, with no hidden fade.
    { Rig r; r.p->set ("amount", 0); r.p->prepareToPlay (SR, 512);
      auto o = run (r, 8000, 512, sine, 0.5f);
      float deviation = 0.0f;
      for (size_t i = 0; i < o[0].size(); ++i)
          deviation = std::max (deviation, std::abs (o[0][i] - sine (0, (int) i, 0.5f)));
      std::printf ("  restored amount 0: max deviation %.2e\n", deviation);
      CHECK (deviation < 1e-6f, "amount zero should pass input unchanged"); }
    { Rig r; r.p->set ("amount", 0); r.p->prepareToPlay (SR, 512);
      run (r, 44100, 512, sine, 0.5f);  // establish steady filter state
      r.p->set ("amount", 1);
      auto o = run (r, 4410, 512, sine, 0.5f);
      float early = 0.0f, late = 0.0f;
      for (int i = 0; i < 10; ++i)
          early = std::max (early, std::abs (o[0][(size_t) i] - sine (0, i, 0.5f)));
      for (int i = 2000; i < 4410; ++i)
          late = std::max (late, std::abs (o[0][(size_t) i] - sine (0, i, 0.5f)));
      std::printf ("  bloom automation: first 10 samples diff %.4f, settled diff %.4f\n", early, late);
      CHECK (early < 0.012f, "amount automation clicked at boundary");
      CHECK (late > 0.005f, "amount never reached requested effect");
      CHECK (finite (o), "nan after automation"); }
    { Rig r; r.p->set ("amount", 0); r.p->prepareToPlay (SR, 512);
      run (r, 4410, 512, sine, 0.5f);
      r.p->set ("output", 12);
      auto o = run (r, 4410, 512, sine, 0.5f);
      float early = 0.0f;
      for (int i = 0; i < 10; ++i)
          early = std::max (early, std::abs (o[0][(size_t) i] - sine (0, i, 0.5f)));
      std::printf ("  output automation: first 10 samples diff %.4f\n", early);
      CHECK (early < 0.04f, "output automation clicked at boundary"); }

    { Rig r; r.p->set ("amount", 0); r.p->prepareToPlay (SR, 512); auto o = run (r, 8000, 512, sine, 0.5f); float d = 0; for (size_t i = 0; i < 8000; ++i) d = std::max (d, std::abs (o[0][i] - sine (0, (int) i, 0.5f)));
      std::printf ("  amount 0 -> max deviation from input %.2e\n", d); CHECK (d < 1e-4f, "not bit-transparent at amount 0"); }
    { Rig r; r.p->set ("amount", 1); r.p->set ("drive", 24); auto o = run (r, 44100, 441, sine, 0.9f);
      float pk = peak (o[0], 4000), dc = mean (o[0], 4000); std::printf ("  max settings: peak %.2f, DC %.5f\n", pk, dc);
      CHECK (finite (o), "nan"); CHECK (std::abs (dc) < 0.01f, "DC offset"); CHECK (pk < 2.5f, "output runaway"); }
    { Rig r; r.p->set ("freq", 12000); r.p->set ("amount", 1); auto o = run (r, 20000, 17, noise, 1.0f); CHECK (finite (o), "nan at 12 kHz / odd block"); }
}
static void testButterfly()
{
    { Rig r; r.p->set ("time", 100); r.p->set ("feedback", 0.5f); r.p->set ("flutter", 0); r.p->set ("spread", 0); r.p->set ("mix", 1); r.p->set ("tone", 18000);
      r.p->prepareToPlay (SR, 512);   // refresh smoother to the new time
      auto o = run (r, 30000, 512, impulse, 1.0f);   // smoother starts at 380 ms, so let time settle first
      (void) o; }
    { Rig r; r.p->set ("time", 380); r.p->set ("feedback", 0.5f); r.p->set ("flutter", 0); r.p->set ("spread", 0); r.p->set ("mix", 1); r.p->set ("tone", 18000);
      auto o = run (r, 60000, 512, impulse, 1.0f); size_t pos = 0; float best = 0; for (size_t i = 100; i < o[0].size(); ++i) if (std::abs (o[0][i]) > best) { best = std::abs (o[0][i]); pos = i; }
      std::printf ("  first echo at sample %zu (expect ~%d), amp %.3f\n", pos, (int) (0.38 * SR), best);
      CHECK (std::abs ((int) pos - (int) (0.38 * SR)) <= 3, "echo timing"); CHECK (best > 0.9f, "echo level"); }
    { Rig r; r.p->set ("time", 380); r.p->set ("flutter", 0); r.p->set ("spread", 1); r.p->set ("mix", 1); r.p->set ("tone", 18000);
      auto o = run (r, 60000, 512, impulse, 1.0f); size_t t1 = (size_t) (0.38 * SR);
      float l1 = peak (std::vector<float> (o[0].begin() + (long) t1 - 4, o[0].begin() + (long) t1 + 4)), r1 = peak (std::vector<float> (o[1].begin() + (long) t1 - 4, o[1].begin() + (long) t1 + 4));
      float r2 = peak (std::vector<float> (o[1].begin() + (long) (2 * t1) - 4, o[1].begin() + (long) (2 * t1) + 4)); std::printf ("  ping-pong: echo1 L %.2f R %.2f, echo2 R %.2f\n", l1, r1, r2);
      CHECK (l1 > 0.9f && r1 < 0.01f, "echo 1 should be left only"); CHECK (r2 > 0.2f, "echo 2 should cross to right"); }
    { Rig r; r.p->set ("feedback", 0.95f); r.p->set ("mix", 1); r.p->set ("time", 5); r.p->set ("flutter", 1); auto o = run (r, 200000, 256, noise, 1.0f);
      float pk = peak (o[0]); std::printf ("  feedback 0.95, 5 ms, full-scale noise: peak %.2f\n", pk); CHECK (finite (o), "nan"); CHECK (pk < 3.0f, "feedback runaway"); }
    { Rig r; r.p->set ("feedback", 0.95f); r.p->set ("mix", 1); auto o = run (r, 44100, 100, impulse, 1.0f); (void) o;
      auto o2 = run (r, 400000, 512, impulse, 0.0f); float tail = peak (o2[0], 399000); std::printf ("  feedback tail after 9 s: %.2e (stays finite & bounded)\n", tail); CHECK (finite (o2), "nan in tail"); }
}
static void testShed()
{
    for (int pitch : { -12, 0, 12 }) for (float scatter : { 0.0f, 1.0f }) for (float glitch : { 0.0f, 1.0f }) for (float size : { 20.0f, 120.0f, 500.0f }) {
      Rig r; r.p->set ("pitch", (float) pitch); r.p->set ("scatter", scatter); r.p->set ("glitch", glitch); r.p->set ("size", size); r.p->set ("mix", 1);
      auto a = run (r, 30000, 512, sine, 0.5f);   // record
      r.p->set ("freeze", 1);
      auto o = run (r, 90000, 480, sine, 0.5f);   // frozen: input keeps playing but must be ignored
      float pk = peak (o[0], 5000), st = maxStep (o[0], 5000); (void) a;
      bool ok = finite (o) && pk < 0.52f && st < 0.3f;
      if (! ok || (pitch == 0 && scatter == 1.0f && glitch == 0.0f)) std::printf ("  pitch %d scatter %.0f glitch %.0f size %.0f -> peak %.2f, max step %.2f\n", pitch, scatter, glitch, size, pk, st);
      CHECK (ok, "frozen output out of bounds / clicky"); }
    { Rig r; r.p->set ("mix", 1); auto o = run (r, 20000, 256, sine, 0.5f); float d = 0; for (size_t i = 0; i < 20000; ++i) d = std::max (d, std::abs (o[0][i] - sine (0, (int) i, 0.5f)));
      std::printf ("  unfrozen passthrough deviation %.2e\n", d); CHECK (d < 1e-6f, "not transparent when unfrozen"); }
    { Rig r; r.p->set ("mix", 1); run (r, 30000, 512, sine, 0.5f); r.p->set ("freeze", 1); auto a = run (r, 20000, 512, sine, 0.5f); r.p->set ("freeze", 0); auto b = run (r, 30000, 512, sine, 0.5f);
      float st = std::max (maxStep (a[0], 3000), maxStep (b[0], 0)); std::printf ("  freeze/unfreeze max step %.2f\n", st); CHECK (st < 0.9f, "click on unfreeze"); }
}
int main (int argc, char** argv)
{
    const char* which = argc > 1 ? argv[1] : "";
    if (! std::strcmp (which, "constrictor")) testConstrictor();
    else if (! std::strcmp (which, "nightbloom")) testNightBloom();
    else if (! std::strcmp (which, "butterfly")) testButterfly();
    else if (! std::strcmp (which, "shed")) testShed();
    std::printf ("%s: %s\n", which, fails ? "FAILED" : "all checks passed");
    return fails ? 1 : 0;
}
