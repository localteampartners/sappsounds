#include "sapp/sounds/PlaybackEngine.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <vector>

namespace sapp::sounds {
namespace {

constexpr int kMaxSelectedPerNote = 64;
constexpr float kSilenceLevel = 1.0e-4f;  // ≈ -80 dBFS

inline float dbToGain(float db) noexcept { return std::pow(10.0f, db * 0.05f); }

struct Xorshift32 {
    uint32_t state = 0x5A9F00D5;
    uint32_t next() noexcept
    {
        uint32_t x = state;
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        return state = x;
    }
    float nextFloat() noexcept  // [0, 1)
    {
        return float(next() >> 8) * (1.0f / 16777216.0f);
    }
};

// Region lookup compiled off the audio thread when an instrument is set.
struct CompiledInstrument {
    InstrumentPtr source;

    // Flattened per-note candidate lists.
    uint32_t attackOffsets[129] = {};
    std::vector<RegionIndex> attackIndices;
    uint32_t releaseOffsets[129] = {};
    std::vector<RegionIndex> releaseIndices;

    const InstrumentDefinition& def() const noexcept { return source->definition; }

    static std::shared_ptr<CompiledInstrument> build(InstrumentPtr inst)
    {
        auto c = std::make_shared<CompiledInstrument>();
        c->source = std::move(inst);
        const auto& regions = c->def().regions;

        auto fill = [&](bool release, uint32_t* offsets, std::vector<RegionIndex>& indices) {
            uint32_t running = 0;
            for (int note = 0; note < 128; ++note) {
                offsets[note] = running;
                for (RegionIndex i = 0; i < regions.size(); ++i) {
                    const auto& r = regions[i];
                    if (r.sample == kInvalidSample) continue;
                    const bool isRelease = r.trigger == TriggerMode::Release ||
                                           r.trigger == TriggerMode::ReleaseKey;
                    if (isRelease != release) continue;
                    if (note >= r.loKey && note <= r.hiKey) {
                        indices.push_back(i);
                        ++running;
                    }
                }
            }
            offsets[128] = running;
        };
        fill(false, c->attackOffsets, c->attackIndices);
        fill(true, c->releaseOffsets, c->releaseIndices);
        return c;
    }
};

enum class EnvStage : uint8_t { Delay, Attack, Hold, Decay, Sustain, Release, Done };

struct PendingStart {
    const RegionDefinition* region = nullptr;
    RegionIndex regionIndex = 0;
    const SampleData* sample = nullptr;
    uint8_t note = 0, velocity = 0;
    float randomTuneCents = 0.0f;
    float randomGainDb = 0.0f;    // amp_random humanize
    float extraDelaySeconds = 0.0f; // delay_random humanize
    float skipSeconds = 0.0f;     // legato attack suppression
    float attackOverride = -1.0f; // legato transition attack, seconds
};

// Crossfade helper: position t in [0,1] through the SFZ curve.
inline float xfCurve(float t, uint8_t curve) noexcept
{
    t = std::clamp(t, 0.0f, 1.0f);
    return curve == 1 ? t : std::sqrt(t);  // gain : power (equal-power)
}

inline float xfSpan(int v, int lo, int hi) noexcept
{
    if (hi <= lo) return v >= lo ? 1.0f : 0.0f;
    return float(v - lo) / float(hi - lo);
}

struct Voice {
    enum class State : uint8_t { Idle, Active, StealFade };
    State state = State::Idle;

    const RegionDefinition* region = nullptr;
    RegionIndex regionIndex = 0;
    const SampleData* sample = nullptr;
    const CompiledInstrument* owner = nullptr;  // snapshot the voice belongs to

    uint8_t note = 0;
    uint8_t velocity = 0;
    bool noteHeld = false;
    bool pedalHeld = false;
    bool isReleaseSample = false;

    double pos = 0.0;
    double baseIncrement = 1.0;   // pitch ratio excluding pitch bend
    int64_t endFrame = 0;         // inclusive last playable frame

    bool looping = false;
    bool loopWhileHeldOnly = false;
    double loopStart = 0.0, loopEndExclusive = 0.0, loopXfade = 0.0;

    EnvStage envStage = EnvStage::Done;
    float envLevel = 0.0f;
    float envAttackStep = 1.0f;
    float envStartLevel = 0.0f;
    float envSustain = 1.0f;
    float envDecayMul = 1.0f;
    float envReleaseMul = 0.0f;
    uint32_t envDelaySamples = 0, envHoldSamples = 0, envStageSamples = 0;

    float gainL = 0.0f, gainR = 0.0f;

    // Live CC crossfade gain (dynamic-layer morphing). Target recomputed per
    // block from controller state; current smoothed per sample.
    bool hasCcXfade = false;
    float xfGain = 1.0f, xfTarget = 1.0f;

    float stealFade = 0.0f, stealFadeStep = 0.0f;
    float lastL = 0.0f, lastR = 0.0f;  // for steal de-click continuation
    bool hasPending = false;
    PendingStart pending;

    uint64_t startedAtFrame = 0;

    bool sounding() const noexcept { return state != State::Idle; }
};

float envelopeStageAdvance(Voice& v) noexcept
{
    switch (v.envStage) {
        case EnvStage::Delay:
            if (v.envStageSamples > 0) { --v.envStageSamples; return 0.0f; }
            v.envStage = EnvStage::Attack;
            v.envLevel = v.envStartLevel;
            [[fallthrough]];
        case EnvStage::Attack:
            v.envLevel += v.envAttackStep;
            if (v.envLevel >= 1.0f) {
                v.envLevel = 1.0f;
                v.envStage = EnvStage::Hold;
                v.envStageSamples = v.envHoldSamples;
            }
            return v.envLevel;
        case EnvStage::Hold:
            if (v.envStageSamples > 0) { --v.envStageSamples; return v.envLevel; }
            v.envStage = EnvStage::Decay;
            [[fallthrough]];
        case EnvStage::Decay:
            v.envLevel = v.envSustain + (v.envLevel - v.envSustain) * v.envDecayMul;
            if (std::abs(v.envLevel - v.envSustain) < 1.0e-4f) {
                v.envLevel = v.envSustain;
                v.envStage = EnvStage::Sustain;
            }
            if (v.envSustain <= kSilenceLevel && v.envLevel <= kSilenceLevel)
                v.envStage = EnvStage::Done;
            return v.envLevel;
        case EnvStage::Sustain:
            if (v.envSustain <= kSilenceLevel) v.envStage = EnvStage::Done;
            return v.envLevel;
        case EnvStage::Release:
            v.envLevel *= v.envReleaseMul;
            if (v.envLevel <= kSilenceLevel) { v.envLevel = 0.0f; v.envStage = EnvStage::Done; }
            return v.envLevel;
        case EnvStage::Done:
        default:
            return 0.0f;
    }
}

// Interpolated fetch with loop-aware neighbours.
template <int Quality>
inline float fetchSample(const float* data, int64_t frames, double pos,
                         bool looping, double loopStart, double loopEndExclusive) noexcept
{
    auto wrap = [&](int64_t i) noexcept -> int64_t {
        if (looping && double(i) >= loopEndExclusive) {
            const double len = loopEndExclusive - loopStart;
            if (len > 0.5) i = int64_t(double(i) - len);
        }
        if (i < 0) return 0;
        if (i >= frames) return frames - 1;
        return i;
    };

    const int64_t i1 = int64_t(pos);
    const float frac = float(pos - double(i1));

    if constexpr (Quality == 0) {
        const float a = data[wrap(i1)];
        const float b = data[wrap(i1 + 1)];
        return a + (b - a) * frac;
    } else {
        const float p0 = data[wrap(i1 - 1)];
        const float p1 = data[wrap(i1)];
        const float p2 = data[wrap(i1 + 1)];
        const float p3 = data[wrap(i1 + 2)];
        // Catmull-Rom
        const float a = 0.5f * (2.0f * p1);
        const float b = 0.5f * (p2 - p0);
        const float c = 0.5f * (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3);
        const float d = 0.5f * (-p0 + 3.0f * p1 - 3.0f * p2 + p3);
        return a + frac * (b + frac * (c + frac * d));
    }
}

} // namespace

// ============================================================== engine impl ==

struct PlaybackEngine::Impl {
    EngineConfig config;
    double sampleRate = 48000.0;

    std::vector<Voice> voices;

    // Snapshot exchange: control thread stores pending, audio thread adopts.
    std::mutex keepAliveMutex;                       // control thread only
    std::vector<std::shared_ptr<CompiledInstrument>> keepAlive;
    std::atomic<CompiledInstrument*> pending{nullptr};
    std::atomic<CompiledInstrument*> audioActive{nullptr};
    const CompiledInstrument* active = nullptr;      // audio thread only
    InstrumentPtr lastSet;                            // control thread only

    // MIDI/controller state (audio thread).
    uint8_t cc[128] = {};
    int16_t bend14 = 0;
    float bendFactor = 1.0f;
    int lastKeyswitch = -1;

    // Note tracking.
    struct NoteState {
        bool held = false;
        bool pedalHeld = false;
        uint8_t velocity = 0;
        uint64_t startFrame = 0;
    };
    NoteState notes[128];
    int heldNoteCount = 0;
    uint16_t rrCounters[128] = {};

    Xorshift32 rng;
    uint64_t engineFrame = 0;

    // Legato state: last musical (non-keyswitch) note for transition targeting;
    // note-ons within the chord window never trigger legato transitions.
    int lastMusicalNote = -1;
    uint64_t lastMusicalNoteFrame = 0;

    std::atomic<int> interpolationQuality{1};
    std::atomic<float> randomTuneCents{0.0f};
    std::atomic<bool> legatoEnabled{false};
    std::atomic<float> legatoSkipSeconds{0.06f};
    std::atomic<float> legatoFadeSeconds{0.045f};
    std::atomic<int> activeVoiceCountPublished{0};

    DiagnosticPublisher publisher;
    DiagnosticSnapshot diag;

    explicit Impl(EngineConfig cfg) : config(cfg)
    {
        voices.resize(size_t(std::clamp(config.maxVoices, 1, 1024)));
        rng.state = config.seed | 1u;
        interpolationQuality.store(config.interpolationQuality);
        randomTuneCents.store(config.randomTuneCents);
    }

    // ---- control thread ----------------------------------------------------

    void setInstrument(InstrumentPtr inst)
    {
        std::shared_ptr<CompiledInstrument> compiled;
        if (inst) compiled = CompiledInstrument::build(inst);
        std::lock_guard<std::mutex> lock(keepAliveMutex);
        lastSet = inst;
        if (compiled) keepAlive.push_back(compiled);
        CompiledInstrument* raw = compiled ? compiled.get() : nullptr;
        pending.store(raw, std::memory_order_release);
    }

    void collectRetired()
    {
        std::lock_guard<std::mutex> lock(keepAliveMutex);
        // Order matters: read pending BEFORE audioActive so a snapshot being
        // adopted between the two loads is seen at least once.
        CompiledInstrument* keep1 = pending.load(std::memory_order_acquire);
        CompiledInstrument* keep2 = audioActive.load(std::memory_order_acquire);
        keepAlive.erase(std::remove_if(keepAlive.begin(), keepAlive.end(),
                                       [&](const auto& p) {
                                           return p.get() != keep1 && p.get() != keep2;
                                       }),
                        keepAlive.end());
    }

    // ---- audio thread ------------------------------------------------------

    void adoptPendingInstrument() noexcept
    {
        CompiledInstrument* p = pending.exchange(nullptr, std::memory_order_acq_rel);
        if (p == nullptr && active != nullptr) return;
        if (p == nullptr) return;
        // Silence all voices from the old snapshot with a fast fade.
        for (auto& v : voices)
            if (v.sounding() && v.owner != p) beginStealFade(v, false);
        active = p;
        audioActive.store(p, std::memory_order_release);
        lastKeyswitch = p->def().defaultKeyswitch;
        for (const auto& d : p->def().controlDefaults) cc[d.cc] = d.value;
    }

    void beginStealFade(Voice& v, bool withPending, float fadeSeconds = -1.0f) noexcept
    {
        if (v.state == Voice::State::Idle) return;
        v.state = Voice::State::StealFade;
        const float seconds = fadeSeconds > 0.0f ? fadeSeconds : config.stealFadeSeconds;
        const float fadeSamples = std::max(8.0f, float(seconds * sampleRate));
        v.stealFade = 1.0f;
        v.stealFadeStep = 1.0f / fadeSamples;
        if (!withPending) v.hasPending = false;
    }

    float velocityGain(const RegionDefinition& r, uint8_t vel) const noexcept
    {
        const float track = r.ampVeltrack * 0.01f;
        const float norm = float(vel) * (1.0f / 127.0f);
        const float curve = norm * norm;  // SFZ default power curve
        return std::clamp(1.0f - track + track * curve, 0.0f, 1.0f);
    }

    void computeGains(const RegionDefinition& r, uint8_t vel, float& gL, float& gR) const noexcept
    {
        const float g = dbToGain(r.volumeDb + r.extraVolumeDb) * velocityGain(r, vel);
        const float panNorm = std::clamp(r.pan * 0.01f, -1.0f, 1.0f);
        const float angle = (panNorm + 1.0f) * 0.78539816f;  // 0..π/2
        gL = g * std::cos(angle) * 1.41421356f * 0.70710678f;
        gR = g * std::sin(angle) * 1.41421356f * 0.70710678f;
    }

    // Product of all static crossfade gains for this note-on (vel + key).
    float staticCrossfadeGain(const RegionDefinition& r, uint8_t note, uint8_t vel) const noexcept
    {
        float g = 1.0f;
        if (r.xfinLoVel >= 0)
            g *= xfCurve(xfSpan(vel, r.xfinLoVel, r.xfinHiVel >= 0 ? r.xfinHiVel : r.xfinLoVel), r.xfVelCurve);
        if (r.xfoutLoVel >= 0)
            g *= xfCurve(1.0f - xfSpan(vel, r.xfoutLoVel, r.xfoutHiVel >= 0 ? r.xfoutHiVel : r.xfoutLoVel), r.xfVelCurve);
        if (r.xfinLoKey >= 0)
            g *= xfCurve(xfSpan(note, r.xfinLoKey, r.xfinHiKey >= 0 ? r.xfinHiKey : r.xfinLoKey), r.xfKeyCurve);
        if (r.xfoutLoKey >= 0)
            g *= xfCurve(1.0f - xfSpan(note, r.xfoutLoKey, r.xfoutHiKey >= 0 ? r.xfoutHiKey : r.xfoutLoKey), r.xfKeyCurve);
        return g;
    }

    // Live CC gain target: crossfades (xfin/xfout) × gain_ccN scaling.
    float ccCrossfadeTarget(const RegionDefinition& r) const noexcept
    {
        float g = 1.0f;
        for (const auto& cf : r.ccCrossfades) {
            const int v = cc[cf.cc];
            if (cf.inLo >= 0)
                g *= xfCurve(xfSpan(v, cf.inLo, cf.inHi >= 0 ? cf.inHi : cf.inLo), r.xfCcCurve);
            if (cf.outLo >= 0)
                g *= xfCurve(1.0f - xfSpan(v, cf.outLo, cf.outHi >= 0 ? cf.outHi : cf.outLo), r.xfCcCurve);
        }
        for (const auto& gc : r.gainCc)
            g *= dbToGain(gc.db * float(cc[gc.cc]) * (1.0f / 127.0f));
        return g;
    }

    void startVoiceNow(Voice& v, const PendingStart& p) noexcept
    {
        const SampleData& s = *p.sample;
        const RegionDefinition& r = *p.region;

        v.state = Voice::State::Active;
        v.region = &r;
        v.regionIndex = p.regionIndex;
        v.sample = &s;
        v.owner = active;
        v.note = p.note;
        v.velocity = p.velocity;
        v.noteHeld = !(r.trigger == TriggerMode::Release || r.trigger == TriggerMode::ReleaseKey ||
                       r.loop.mode == LoopMode::OneShot);
        v.pedalHeld = false;
        v.isReleaseSample = r.trigger == TriggerMode::Release || r.trigger == TriggerMode::ReleaseKey;
        v.startedAtFrame = engineFrame;
        v.hasPending = false;
        v.stealFade = 0.0f;

        // Pitch.
        const double semis = double(p.note) - double(r.rootKey) + double(r.transpose) +
                             (double(r.tuneCents) + double(p.randomTuneCents)) * 0.01;
        v.baseIncrement = std::pow(2.0, semis / 12.0) * (double(s.sampleRate) / sampleRate);

        // Playback bounds (legato may skip past the recorded attack).
        v.pos = double(std::clamp<int64_t>(r.offset, 0, int64_t(s.frames) - 1));
        v.endFrame = (r.end >= 0 && r.end < int64_t(s.frames)) ? r.end : int64_t(s.frames) - 1;
        if (p.skipSeconds > 0.0f) {
            const double skip = double(p.skipSeconds) * double(s.sampleRate);
            const double cap = double(v.endFrame) * 0.5;
            v.pos = std::min(v.pos + skip, cap);
        }

        // Loop resolution: explicit region points → embedded smpl → none.
        int64_t ls = r.loop.start, le = r.loop.end;
        if (ls < 0 && s.embeddedLoop.hasLoop) ls = int64_t(s.embeddedLoop.start);
        if (le < 0 && s.embeddedLoop.hasLoop) le = int64_t(s.embeddedLoop.end);
        LoopMode mode = r.loop.mode;
        if (!r.loop.explicitMode)
            mode = (ls >= 0 && le > ls) ? LoopMode::Continuous : LoopMode::NoLoop;
        const bool loopValid = ls >= 0 && le > ls && le <= v.endFrame;
        v.looping = loopValid && (mode == LoopMode::Continuous || mode == LoopMode::Sustain);
        v.loopWhileHeldOnly = mode == LoopMode::Sustain;
        v.loopStart = double(std::max<int64_t>(0, ls));
        v.loopEndExclusive = double(le + 1);
        v.loopXfade = std::min(double(r.loop.crossfadeSeconds) * s.sampleRate,
                               v.loopEndExclusive - v.loopStart);

        // Envelope.
        const auto& e = r.ampeg;
        v.envDelaySamples = uint32_t(std::max(0.0f, e.delay + p.extraDelaySeconds) * float(sampleRate));
        v.envHoldSamples = uint32_t(std::max(0.0f, e.hold) * float(sampleRate));
        v.envStartLevel = std::clamp(e.start, 0.0f, 1.0f);
        v.envSustain = std::clamp(e.sustain, 0.0f, 1.0f);
        const float attackSeconds = p.attackOverride >= 0.0f ? p.attackOverride : e.attack;
        const float attackSamples = std::max(1.0f, attackSeconds * float(sampleRate));
        v.envAttackStep = (1.0f - v.envStartLevel) / attackSamples;
        if (attackSeconds <= 0.0005f) v.envAttackStep = 1.0f;
        const float decaySamples = std::max(1.0f, e.decay * float(sampleRate));
        v.envDecayMul = std::exp(-6.91f / decaySamples);
        const float releaseSamples = std::max(1.0f, e.release * float(sampleRate));
        v.envReleaseMul = std::exp(-6.91f / releaseSamples);
        if (v.envDelaySamples > 0) {
            v.envStage = EnvStage::Delay;
            v.envStageSamples = v.envDelaySamples;
            v.envLevel = 0.0f;
        } else {
            v.envStage = EnvStage::Attack;
            v.envLevel = v.envStartLevel;
        }

        computeGains(r, p.velocity, v.gainL, v.gainR);
        const float staticXf = staticCrossfadeGain(r, p.note, p.velocity) *
                               dbToGain(p.randomGainDb);
        v.gainL *= staticXf;
        v.gainR *= staticXf;

        v.hasCcXfade = !r.ccCrossfades.empty() || !r.gainCc.empty();
        v.xfTarget = v.hasCcXfade ? ccCrossfadeTarget(r) : 1.0f;
        v.xfGain = v.xfTarget;  // start at the current controller position
    }

    // Musical fast release for legato transitions (independent of steal fades).
    void releaseVoiceOver(Voice& v, float seconds) noexcept
    {
        if (v.state != Voice::State::Active) return;
        v.noteHeld = false;
        v.pedalHeld = false;
        if (v.loopWhileHeldOnly) v.looping = false;
        const float samples = std::max(8.0f, seconds * float(sampleRate));
        v.envReleaseMul = std::exp(-6.91f / samples);
        if (v.envStage != EnvStage::Done) v.envStage = EnvStage::Release;
    }

    void releaseVoice(Voice& v) noexcept
    {
        if (v.state != Voice::State::Active) return;
        v.noteHeld = false;
        v.pedalHeld = false;
        if (v.loopWhileHeldOnly) v.looping = false;  // sustain loop → run to end
        if (v.envStage != EnvStage::Done) v.envStage = EnvStage::Release;
    }

    Voice* allocateVoice() noexcept
    {
        // 1) free voice
        for (auto& v : voices)
            if (v.state == Voice::State::Idle) return &v;

        // 2) steal by priority: quiet release tails → old quiet → oldest.
        Voice* best = nullptr;
        float bestScore = 1.0e30f;
        for (auto& v : voices) {
            if (v.state == Voice::State::StealFade) continue;  // already going
            float score = v.envLevel * 100.0f;
            if (v.envStage != EnvStage::Release) score += 1000.0f;
            if (v.isReleaseSample) score -= 500.0f;
            score -= float(engineFrame - v.startedAtFrame) * 1.0e-6f;  // older = lower
            if (score < bestScore) { bestScore = score; best = &v; }
        }
        if (best != nullptr) ++diag.voicesStolen;
        return best;
    }

    void triggerRegion(const RegionDefinition& r, RegionIndex index,
                       uint8_t note, uint8_t velocity, bool useOnVelocity,
                       float skipSeconds = 0.0f, float attackOverride = -1.0f) noexcept
    {
        const auto& samples = active->source->samples;
        if (r.sample < 0 || size_t(r.sample) >= samples.size()) return;
        const SampleData& s = samples[size_t(r.sample)];
        if (!s.valid()) return;

        // off_by chokes: a new region in group G silences voices with off_by == G.
        if (r.group != 0) {
            for (auto& v : voices) {
                if (!v.sounding() || v.region == nullptr) continue;
                if (v.region->offBy == r.group && v.region != &r) {
                    if (v.region->offMode == OffMode::Fast) beginStealFade(v, false);
                    else if (v.region->offMode == OffMode::Time)
                        beginStealFade(v, false, v.region->offTime);
                    else releaseVoice(v);
                }
            }
        }

        // note_polyphony: cap simultaneous voices for this note+region.
        if (r.notePolyphony > 0) {
            int count = 0;
            Voice* oldest = nullptr;
            for (auto& v : voices) {
                if (v.state == Voice::State::Active && v.note == note && v.region == &r) {
                    ++count;
                    if (oldest == nullptr || v.startedAtFrame < oldest->startedAtFrame)
                        oldest = &v;
                }
            }
            if (count >= r.notePolyphony && oldest != nullptr)
                beginStealFade(*oldest, false);
        }

        Voice* v = allocateVoice();
        if (v == nullptr) return;

        PendingStart p;
        p.region = &r;
        p.regionIndex = index;
        p.sample = &s;
        p.note = note;
        p.velocity = useOnVelocity ? velocity : velocity;
        p.skipSeconds = skipSeconds;
        p.attackOverride = attackOverride;
        const float tuneBreadth = randomTuneCents.load(std::memory_order_relaxed);
        p.randomTuneCents = tuneBreadth > 0.0f ? (rng.nextFloat() * 2.0f - 1.0f) * tuneBreadth : 0.0f;

        // SFZ per-note humanize (deterministic under the engine seed).
        if (r.pitchRandomCents > 0.0f)
            p.randomTuneCents += (rng.nextFloat() - 0.5f) * r.pitchRandomCents;
        if (r.ampRandomDb > 0.0f)
            p.randomGainDb = (rng.nextFloat() - 0.5f) * r.ampRandomDb;
        if (r.delayRandomSeconds > 0.0f)
            p.extraDelaySeconds = rng.nextFloat() * r.delayRandomSeconds;

        if (v->state == Voice::State::Idle) {
            startVoiceNow(*v, p);
        } else {
            // Voice is live: fade fast, then start pending payload.
            v->hasPending = true;
            v->pending = p;
            beginStealFade(*v, true);
        }
    }

    void noteOn(uint8_t note, uint8_t velocity) noexcept
    {
        if (active == nullptr) return;
        const auto& def = active->def();
        ++diag.notesOn;

        NoteDecision& nd = diag.lastNote;
        nd = {};
        nd.engineFrame = engineFrame;
        nd.note = note;
        nd.velocity = velocity;
        nd.activeKeyswitch = int8_t(lastKeyswitch);

        // Keyswitch handling.
        if (def.keyswitchLo >= 0 && note >= def.keyswitchLo && note <= def.keyswitchHi) {
            lastKeyswitch = note;
            nd.wasKeyswitch = true;
            nd.activeKeyswitch = int8_t(note);
        }

        const uint32_t begin = active->attackOffsets[note];
        const uint32_t end = active->attackOffsets[note + 1];
        const float roll = rng.nextFloat();
        const uint16_t counter = rrCounters[note];

        const RegionDefinition* selected[kMaxSelectedPerNote];
        RegionIndex selectedIndex[kMaxSelectedPerNote];
        int selectedCount = 0;

        for (uint32_t k = begin; k < end; ++k) {
            const RegionIndex ri = active->attackIndices[k];
            const RegionDefinition& r = def.regions[ri];
            RejectReason reason = RejectReason::None;

            if (velocity < r.loVel || velocity > r.hiVel) reason = RejectReason::Velocity;
            else if (r.swLast >= 0 && r.swLast != lastKeyswitch) reason = RejectReason::Keyswitch;
            else if (r.trigger == TriggerMode::First && heldNoteCount > 0) reason = RejectReason::TriggerMode;
            else if (r.trigger == TriggerMode::Legato && heldNoteCount == 0) reason = RejectReason::TriggerMode;
            else if (r.seqLength > 1 && (counter % r.seqLength) + 1 != r.seqPosition) reason = RejectReason::Sequence;
            else if (!(r.loRand <= roll && (roll < r.hiRand || (r.hiRand >= 1.0f && roll <= 1.0f)))) reason = RejectReason::Random;
            else {
                for (const auto& c : r.ccConditions) {
                    if (cc[c.cc] < c.lo || cc[c.cc] > c.hi) { reason = RejectReason::CcCondition; break; }
                }
            }

            if (nd.candidateCount < NoteDecision::kMaxRecorded)
                nd.decisions[nd.candidateCount] = {ri, reason};
            if (nd.candidateCount < 255) ++nd.candidateCount;

            if (reason == RejectReason::None && selectedCount < kMaxSelectedPerNote) {
                selected[selectedCount] = &r;
                selectedIndex[selectedCount] = ri;
                ++selectedCount;
            }
        }

        nd.selectedCount = uint8_t(selectedCount);
        if (end > begin) ++rrCounters[note];

        // Legato level 2: overlapping single-line playing suppresses the new
        // note's recorded attack and fades the previous note out musically.
        // Chord-guard: near-simultaneous note-ons are a chord, not a slur.
        float skipSeconds = 0.0f, attackOverride = -1.0f;
        const bool wantLegato = legatoEnabled.load(std::memory_order_relaxed);
        if (wantLegato && selectedCount > 0 && !nd.wasKeyswitch &&
            heldNoteCount > 0 && lastMusicalNote >= 0 && lastMusicalNote != note &&
            notes[lastMusicalNote].held &&
            engineFrame - lastMusicalNoteFrame > uint64_t(0.03 * sampleRate)) {
            const float fade = legatoFadeSeconds.load(std::memory_order_relaxed);
            skipSeconds = legatoSkipSeconds.load(std::memory_order_relaxed);
            attackOverride = fade;
            for (auto& v : voices)
                if (v.state == Voice::State::Active && v.note == lastMusicalNote &&
                    !v.isReleaseSample)
                    releaseVoiceOver(v, fade);
        }
        if (!nd.wasKeyswitch && selectedCount > 0) {
            lastMusicalNote = note;
            lastMusicalNoteFrame = engineFrame;
        }

        auto& ns = notes[note];
        if (!ns.held) ++heldNoteCount;
        ns.held = true;
        ns.pedalHeld = false;
        ns.velocity = velocity;
        ns.startFrame = engineFrame;

        for (int i = 0; i < selectedCount; ++i)
            triggerRegion(*selected[i], selectedIndex[i], note, velocity, true,
                          skipSeconds, attackOverride);
    }

    void triggerReleaseSamples(uint8_t note, uint8_t onVelocity, bool pedalDown) noexcept
    {
        if (active == nullptr) return;
        const auto& def = active->def();
        const uint32_t begin = active->releaseOffsets[note];
        const uint32_t end = active->releaseOffsets[note + 1];
        const float roll = rng.nextFloat();
        const uint16_t counter = rrCounters[note];

        for (uint32_t k = begin; k < end; ++k) {
            const RegionIndex ri = active->releaseIndices[k];
            const RegionDefinition& r = def.regions[ri];
            // trigger=release waits for pedal-up; release_key fires regardless.
            if (r.trigger == TriggerMode::Release && pedalDown) continue;
            if (onVelocity < r.loVel || onVelocity > r.hiVel) continue;
            if (r.swLast >= 0 && r.swLast != lastKeyswitch) continue;
            if (r.seqLength > 1 && (counter % r.seqLength) + 1 != r.seqPosition) continue;
            if (!(r.loRand <= roll && (roll < r.hiRand || (r.hiRand >= 1.0f && roll <= 1.0f)))) continue;
            bool ccOk = true;
            for (const auto& c : r.ccConditions)
                if (cc[c.cc] < c.lo || cc[c.cc] > c.hi) { ccOk = false; break; }
            if (!ccOk) continue;
            triggerRegion(r, ri, note, onVelocity, false);
        }
    }

    void noteOff(uint8_t note) noexcept
    {
        auto& ns = notes[note];
        if (!ns.held) return;
        ns.held = false;
        if (heldNoteCount > 0) --heldNoteCount;

        const bool pedalDown = cc[64] >= 64;
        if (pedalDown) {
            ns.pedalHeld = true;
            for (auto& v : voices)
                if (v.state == Voice::State::Active && v.note == note && v.noteHeld && !v.isReleaseSample) {
                    v.noteHeld = false;
                    v.pedalHeld = true;
                }
            // release_key samples fire even under pedal.
            triggerReleaseSamples(note, ns.velocity, true);
            return;
        }

        for (auto& v : voices)
            if (v.state == Voice::State::Active && v.note == note && (v.noteHeld || v.pedalHeld))
                releaseVoice(v);
        triggerReleaseSamples(note, ns.velocity, false);
    }

    void pedalUp() noexcept
    {
        for (int note = 0; note < 128; ++note) {
            auto& ns = notes[note];
            if (!ns.pedalHeld) continue;
            ns.pedalHeld = false;
            for (auto& v : voices)
                if (v.state == Voice::State::Active && v.note == note && v.pedalHeld)
                    releaseVoice(v);
            triggerReleaseSamples(uint8_t(note), ns.velocity, false);
        }
    }

    void allNotesOff(bool hard) noexcept
    {
        for (auto& v : voices) {
            if (!v.sounding()) continue;
            if (hard) beginStealFade(v, false);
            else releaseVoice(v);
        }
        for (auto& ns : notes) { ns.held = false; ns.pedalHeld = false; }
        heldNoteCount = 0;
    }

    void handleEvent(const MidiEvent& e) noexcept
    {
        switch (e.type) {
            case MidiEvent::Type::NoteOn:
                if (e.value == 0) { noteOff(e.note); break; }
                noteOn(e.note, e.value);
                break;
            case MidiEvent::Type::NoteOff:
                noteOff(e.note);
                break;
            case MidiEvent::Type::Controller: {
                const uint8_t num = e.note;
                const uint8_t old = cc[num];
                cc[num] = e.value;
                if (num == 64 && old >= 64 && e.value < 64) pedalUp();
                if (num == 120) allNotesOff(true);
                if (num == 123) allNotesOff(false);
                break;
            }
            case MidiEvent::Type::PitchBend:
                bend14 = e.bend14;
                bendFactor = float(std::pow(2.0, double(bend14) / 8192.0 *
                                                     double(config.pitchBendRangeSemitones) / 12.0));
                break;
            case MidiEvent::Type::AllNotesOff:
                allNotesOff(false);
                break;
            case MidiEvent::Type::AllSoundOff:
                allNotesOff(true);
                break;
        }
    }

    float xfSmoothCoef = 0.002f;

    template <int Quality>
    void renderVoiceSegment(Voice& v, float* outL, float* outR, int frames) noexcept
    {
        const SampleData& s = *v.sample;
        const float* ch0 = s.data[0].data();
        const float* ch1 = s.channels > 1 ? s.data[1].data() : nullptr;
        const int64_t sframes = int64_t(s.frames);
        const double increment = v.baseIncrement * double(bendFactor);
        const double loopLen = v.loopEndExclusive - v.loopStart;

        for (int f = 0; f < frames; ++f) {
            if (v.state == Voice::State::StealFade) {
                // Short linear fade on the held output values, then restart/idle.
                outL[f] += v.lastL * v.stealFade;
                outR[f] += v.lastR * v.stealFade;
                v.stealFade -= v.stealFadeStep;
                if (v.stealFade <= 0.0f) {
                    if (v.hasPending) startVoiceNow(v, v.pending);
                    else { v.state = Voice::State::Idle; return; }
                }
                continue;
            }

            const float env = envelopeStageAdvance(v);
            if (v.envStage == EnvStage::Done) { v.state = Voice::State::Idle; return; }
            if (v.envStage == EnvStage::Delay) continue;  // hold position; attack stays intact

            float l = fetchSample<Quality>(ch0, sframes, v.pos, v.looping, v.loopStart, v.loopEndExclusive);
            float r = ch1 != nullptr
                          ? fetchSample<Quality>(ch1, sframes, v.pos, v.looping, v.loopStart, v.loopEndExclusive)
                          : l;

            // Loop crossfade zone: blend with pre-loop material for a seamless join.
            if (v.looping && v.loopXfade > 0.0 && loopLen > 0.0) {
                const double intoXfade = v.pos - (v.loopEndExclusive - v.loopXfade);
                if (intoXfade >= 0.0) {
                    const float t = float(intoXfade / v.loopXfade);
                    const double prePos = v.pos - loopLen;
                    if (prePos >= 0.0) {
                        const float pl = fetchSample<Quality>(ch0, sframes, prePos, false, 0.0, 0.0);
                        const float pr = ch1 != nullptr ? fetchSample<Quality>(ch1, sframes, prePos, false, 0.0, 0.0) : pl;
                        l = l * (1.0f - t) + pl * t;
                        r = r * (1.0f - t) + pr * t;
                    }
                }
            }

            // Live crossfade smoothing (~8 ms) keeps CC1 morphs zipper-free.
            if (v.hasCcXfade)
                v.xfGain += xfSmoothCoef * (v.xfTarget - v.xfGain);

            const float sl = l * v.gainL * env * v.xfGain;
            const float sr = r * v.gainR * env * v.xfGain;
            outL[f] += sl;
            outR[f] += sr;
            v.lastL = sl;
            v.lastR = sr;

            v.pos += increment;
            if (v.looping && v.pos >= v.loopEndExclusive && loopLen > 0.0) {
                v.pos -= loopLen;
            } else if (v.pos > double(v.endFrame)) {
                v.state = Voice::State::Idle;
                return;
            }
        }
    }

    void renderSegment(float* outL, float* outR, int frames) noexcept
    {
        if (frames <= 0) return;
        const int quality = interpolationQuality.load(std::memory_order_relaxed);
        xfSmoothCoef = 1.0f - std::exp(-1.0f / (0.008f * float(sampleRate)));
        int active_count = 0;
        for (auto& v : voices) {
            if (!v.sounding()) continue;
            ++active_count;
            if (v.hasCcXfade && v.region != nullptr)
                v.xfTarget = ccCrossfadeTarget(*v.region);
            if (quality == 0) renderVoiceSegment<0>(v, outL, outR, frames);
            else renderVoiceSegment<1>(v, outL, outR, frames);
        }
        diag.activeVoices = uint32_t(active_count);
        diag.peakVoices = std::max(diag.peakVoices, diag.activeVoices);
        engineFrame += uint64_t(frames);
    }

    void process(const MidiEvent* events, int eventCount, float* outL, float* outR, int frames) noexcept
    {
        adoptPendingInstrument();
        if (active == nullptr) {
            activeVoiceCountPublished.store(0, std::memory_order_relaxed);
            return;
        }

        int cursor = 0;
        for (int i = 0; i < eventCount; ++i) {
            const int at = int(std::min<uint32_t>(events[i].frame, uint32_t(frames)));
            if (at > cursor) {
                renderSegment(outL + cursor, outR + cursor, at - cursor);
                cursor = at;
            }
            handleEvent(events[i]);
        }
        if (cursor < frames)
            renderSegment(outL + cursor, outR + cursor, frames - cursor);

        float peakL = 0.0f, peakR = 0.0f;
        for (int f = 0; f < frames; ++f) {
            peakL = std::max(peakL, std::abs(outL[f]));
            peakR = std::max(peakR, std::abs(outR[f]));
        }
        diag.lastPeakL = peakL;
        diag.lastPeakR = peakR;
        diag.activeKeyswitch = int8_t(lastKeyswitch);
        publisher.publish(diag);
        activeVoiceCountPublished.store(int(diag.activeVoices), std::memory_order_relaxed);
    }
};

// ================================================================== facade ==

PlaybackEngine::PlaybackEngine() : PlaybackEngine(EngineConfig{}) {}
PlaybackEngine::PlaybackEngine(EngineConfig config) : impl_(std::make_unique<Impl>(config)) {}
PlaybackEngine::~PlaybackEngine() = default;

void PlaybackEngine::prepare(double sampleRate, int maxBlockFrames)
{
    impl_->sampleRate = std::max(8000.0, sampleRate);
    impl_->config.maxBlockFrames = maxBlockFrames;
    for (auto& v : impl_->voices) v = Voice{};
    for (auto& ns : impl_->notes) ns = Impl::NoteState{};
    impl_->heldNoteCount = 0;
    impl_->engineFrame = 0;
}

void PlaybackEngine::setInstrument(InstrumentPtr instrument) { impl_->setInstrument(std::move(instrument)); }
void PlaybackEngine::collectRetired() { impl_->collectRetired(); }

InstrumentPtr PlaybackEngine::currentInstrument() const
{
    std::lock_guard<std::mutex> lock(impl_->keepAliveMutex);
    return impl_->lastSet;
}

void PlaybackEngine::setInterpolationQuality(int quality)
{
    impl_->interpolationQuality.store(std::clamp(quality, 0, 1));
}
void PlaybackEngine::setRandomTuneCents(float cents)
{
    impl_->randomTuneCents.store(std::clamp(cents, 0.0f, 100.0f));
}
void PlaybackEngine::setLegato(bool enabled, float skipSeconds, float fadeSeconds) noexcept
{
    impl_->legatoEnabled.store(enabled, std::memory_order_relaxed);
    impl_->legatoSkipSeconds.store(std::clamp(skipSeconds, 0.0f, 0.5f), std::memory_order_relaxed);
    impl_->legatoFadeSeconds.store(std::clamp(fadeSeconds, 0.005f, 0.5f), std::memory_order_relaxed);
}

void PlaybackEngine::resetSequences()
{
    std::memset(impl_->rrCounters, 0, sizeof(impl_->rrCounters));
}
void PlaybackEngine::reseed(uint32_t seed) { impl_->rng.state = seed | 1u; }

void PlaybackEngine::process(const MidiEvent* events, int eventCount,
                             float* outL, float* outR, int frames) noexcept
{
    impl_->process(events, eventCount, outL, outR, frames);
}

const DiagnosticPublisher& PlaybackEngine::diagnostics() const { return impl_->publisher; }
int PlaybackEngine::activeVoiceCount() const noexcept
{
    return impl_->activeVoiceCountPublished.load(std::memory_order_relaxed);
}
const EngineConfig& PlaybackEngine::config() const noexcept { return impl_->config; }

} // namespace sapp::sounds
