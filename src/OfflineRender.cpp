#include "sapp/sounds/OfflineRender.h"

#include <algorithm>
#include <cmath>

namespace sapp::sounds {

RenderOutput renderOffline(const InstrumentPtr& instrument,
                           const std::vector<TimedMidiEvent>& events,
                           const OfflineRenderOptions& options)
{
    RenderOutput out;
    out.sampleRate = options.sampleRate;
    if (!instrument) return out;

    EngineConfig config;
    config.maxVoices = options.maxVoices;
    config.interpolationQuality = options.interpolationQuality;
    config.seed = options.seed;
    config.randomTuneCents = options.randomTuneCents;

    PlaybackEngine engine(config);
    engine.prepare(options.sampleRate, options.blockFrames);
    engine.setInstrument(instrument);

    double lastEvent = 0.0;
    for (const auto& e : events) lastEvent = std::max(lastEvent, e.seconds);
    const uint64_t totalFrames =
        uint64_t((lastEvent + std::max(0.0, options.tailSeconds)) * options.sampleRate) + 1;

    out.left.assign(size_t(totalFrames), 0.0f);
    out.right.assign(size_t(totalFrames), 0.0f);

    std::vector<MidiEvent> block;
    block.reserve(256);

    size_t nextEvent = 0;
    std::vector<TimedMidiEvent> sorted = events;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const auto& a, const auto& b) { return a.seconds < b.seconds; });

    for (uint64_t frame = 0; frame < totalFrames; frame += uint64_t(options.blockFrames)) {
        const int frames = int(std::min<uint64_t>(uint64_t(options.blockFrames), totalFrames - frame));
        block.clear();
        const double blockStart = double(frame) / options.sampleRate;
        const double blockEnd = double(frame + uint64_t(frames)) / options.sampleRate;
        while (nextEvent < sorted.size() && sorted[nextEvent].seconds < blockEnd) {
            const auto& e = sorted[nextEvent];
            if (e.seconds >= blockStart || frame == 0) {
                MidiEvent m;
                m.frame = uint32_t(std::clamp(int64_t(e.seconds * options.sampleRate) - int64_t(frame),
                                              int64_t(0), int64_t(frames - 1)));
                switch (e.status) {
                    case 0x90: m.type = MidiEvent::Type::NoteOn; m.note = e.data1; m.value = e.data2; break;
                    case 0x80: m.type = MidiEvent::Type::NoteOff; m.note = e.data1; m.value = 0; break;
                    case 0xB0: m.type = MidiEvent::Type::Controller; m.note = e.data1; m.value = e.data2; break;
                    case 0xE0: m.type = MidiEvent::Type::PitchBend; m.bend14 = e.bend14; break;
                    default: ++nextEvent; continue;
                }
                block.push_back(m);
            }
            ++nextEvent;
        }
        engine.process(block.data(), int(block.size()),
                       out.left.data() + frame, out.right.data() + frame, frames);
    }

    double sumSq = 0.0;
    for (size_t i = 0; i < out.left.size(); ++i) {
        out.left[i] *= options.masterGain;
        out.right[i] *= options.masterGain;
        out.peak = std::max({out.peak, std::abs(out.left[i]), std::abs(out.right[i])});
        sumSq += double(out.left[i]) * out.left[i] + double(out.right[i]) * out.right[i];
    }
    out.rms = float(std::sqrt(sumSq / double(std::max<size_t>(1, out.left.size() * 2))));
    return out;
}

} // namespace sapp::sounds
