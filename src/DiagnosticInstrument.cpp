#include "sapp/sounds/DiagnosticInstrument.h"

#include <cmath>
#include <random>

namespace sapp::sounds {
namespace {

constexpr double kPi = 3.14159265358979323846;

double noteHz(int note) { return 440.0 * std::pow(2.0, (note - 69) / 12.0); }

// Additive tone with per-partial decay — string-section-ish when slow,
// plucked when fast. Deterministic per (note, seed, variant).
SampleData synthesizeTone(int rootNote, uint32_t sampleRate, float seconds,
                          bool sustained, bool pluck, int velLayer, uint32_t seed, int variant)
{
    SampleData s;
    s.sampleRate = sampleRate;
    s.channels = 2;
    const uint64_t frames = uint64_t(double(sampleRate) * seconds);
    s.frames = frames;
    s.data.assign(2, std::vector<float>(size_t(frames), 0.0f));

    std::mt19937 rng(seed ^ uint32_t(rootNote * 7919) ^ uint32_t(variant * 104729));
    std::uniform_real_distribution<double> unit(0.0, 1.0);

    const double f0 = noteHz(rootNote);
    const int partials = std::min(24, int(double(sampleRate) * 0.45 / f0));
    const double brightness = velLayer == 0 ? 0.55 : 1.0;

    struct Partial { double freq, amp, phase, pan, decay, vibDepth; };
    std::vector<Partial> ps;
    for (int k = 1; k <= partials; ++k) {
        Partial p;
        const double detune = 1.0 + (unit(rng) - 0.5) * 0.0015;  // ensemble shimmer
        p.freq = f0 * k * detune;
        const double rolloff = std::pow(double(k), sustained ? 1.35 : 1.1);
        p.amp = brightness / rolloff * (k % 2 == 0 ? 0.85 : 1.0);
        if (velLayer == 0 && k > 6) p.amp *= std::exp(-(k - 6) * 0.35);  // darker pp layer
        p.phase = unit(rng) * 2.0 * kPi;
        p.pan = 0.5 + (unit(rng) - 0.5) * 0.7;
        p.decay = pluck ? (2.2 + 0.9 * k) : 0.0;  // per-partial decay for plucks
        p.vibDepth = sustained ? 0.0035 : 0.0;
        ps.push_back(p);
    }

    const double vibRate = 5.1 + unit(rng) * 0.6;
    const double attack = sustained ? 0.06 : 0.004;
    const double norm = 0.5 / std::sqrt(double(partials));

    for (uint64_t i = 0; i < frames; ++i) {
        const double t = double(i) / double(sampleRate);
        const double vib = std::sin(2.0 * kPi * vibRate * t);
        const double envAtt = t < attack ? t / attack : 1.0;
        double l = 0.0, r = 0.0;
        for (const auto& p : ps) {
            const double freq = p.freq * (1.0 + p.vibDepth * vib * std::min(1.0, t / 0.6));
            const double amp = p.amp * (p.decay > 0.0 ? std::exp(-t * p.decay / 2.0) : 1.0);
            const double v = std::sin(2.0 * kPi * freq * t + p.phase) * amp;
            l += v * (1.0 - p.pan);
            r += v * p.pan;
        }
        // Gentle body resonance via soft saturation of the sum.
        l = std::tanh(l * norm * envAtt * 1.4);
        r = std::tanh(r * norm * envAtt * 1.4);
        s.data[0][size_t(i)] = float(l);
        s.data[1][size_t(i)] = float(r);
    }

    if (sustained) {
        // Loop the stable middle portion. Proportional bounds keep the loop
        // valid for any duration; guard against degenerate windows.
        const uint64_t loopStart = std::min(uint64_t(double(sampleRate) * 0.5), frames / 3);
        const uint64_t loopEnd = frames - std::min(uint64_t(double(sampleRate) * 0.05), frames / 10) - 1;
        if (loopEnd > loopStart + sampleRate / 20) {
            s.embeddedLoop.hasLoop = true;
            s.embeddedLoop.start = uint32_t(loopStart);
            s.embeddedLoop.end = uint32_t(loopEnd);
        }
    }

    // Normalize to a consistent, healthy level (deterministic).
    float rawPeak = 0.0f;
    for (size_t c = 0; c < 2; ++c)
        for (float v : s.data[c]) rawPeak = std::max(rawPeak, std::abs(v));
    const float target = 0.7f;
    const float gain = rawPeak > 1.0e-6f ? target / rawPeak : 1.0f;
    double sumSq = 0.0;
    for (size_t c = 0; c < 2; ++c)
        for (float& v : s.data[c]) { v *= gain; sumSq += double(v) * v; }
    s.peak = target;
    s.rms = float(std::sqrt(sumSq / double(frames * 2)));
    return s;
}

} // namespace

InstrumentPtr makeDiagnosticInstrument(const DiagnosticInstrumentOptions& options)
{
    auto inst = std::make_shared<LoadedInstrument>();
    auto& def = inst->definition;
    def.name = "Diagnostic Orchestra";
    def.sourcePath = "";

    struct Articulation { const char* label; int keyswitch; bool sustained; bool pluck; int roundRobins; };
    const Articulation arts[] = {
        {"Sustain", 12, true, false, 1},
        {"Staccato", 13, false, false, 2},
        {"Pizzicato", 14, false, true, 2},
    };

    const int rootStep = 7;
    const int loRoot = 24, hiRoot = 96;

    for (const auto& art : arts) {
        for (int root = loRoot; root <= hiRoot; root += rootStep) {
            for (int vel = 0; vel < 2; ++vel) {
                for (int rr = 0; rr < art.roundRobins; ++rr) {
                    SampleData s = synthesizeTone(
                        root, options.sampleRate,
                        art.sustained ? options.sustainSeconds : options.shortSeconds,
                        art.sustained, art.pluck, vel, options.seed, rr);
                    s.relativePath = std::string("diag/") + art.label + "_" + std::to_string(root) +
                                     "_v" + std::to_string(vel) + "_rr" + std::to_string(rr) + ".gen";
                    inst->samples.push_back(std::move(s));

                    RegionDefinition r;
                    r.sample = SampleIndex(inst->samples.size() - 1);
                    r.samplePath = inst->samples.back().relativePath;
                    r.rootKey = uint8_t(root);
                    r.loKey = uint8_t(std::max(0, root - rootStep / 2));
                    r.hiKey = uint8_t(std::min(127, root + (rootStep - 1) / 2));
                    r.loVel = vel == 0 ? 0 : 96;
                    r.hiVel = vel == 0 ? 95 : 127;
                    r.swLoKey = 12;
                    r.swHiKey = 14;
                    r.swLast = art.keyswitch;
                    r.swDefault = 12;
                    r.swLabel = art.label;
                    r.seqLength = uint16_t(art.roundRobins);
                    r.seqPosition = uint16_t(rr + 1);
                    if (art.sustained) {
                        r.loop.mode = LoopMode::Sustain;
                        r.loop.explicitMode = true;
                        r.loop.crossfadeSeconds = 0.08f;
                        r.ampeg.attack = 0.02f;
                        r.ampeg.release = 0.45f;
                    } else {
                        r.ampeg.attack = 0.001f;
                        r.ampeg.release = art.pluck ? 0.35f : 0.12f;
                    }
                    r.volumeDb = -6.0f;
                    def.regions.push_back(std::move(r));
                }
            }
        }
    }

    def.keyswitchLo = 12;
    def.keyswitchHi = 14;
    def.defaultKeyswitch = 12;
    def.loKeyUsed = uint8_t(loRoot - rootStep / 2);
    def.hiKeyUsed = uint8_t(hiRoot + rootStep / 2);
    for (const auto& art : arts) {
        ArticulationInfo a;
        a.name = art.label;
        a.keyswitch = art.keyswitch;
        a.isDefault = art.keyswitch == 12;
        for (const auto& r : def.regions)
            if (r.swLast == art.keyswitch) ++a.regionCount;
        def.articulations.push_back(a);
    }
    return inst;
}

} // namespace sapp::sounds
