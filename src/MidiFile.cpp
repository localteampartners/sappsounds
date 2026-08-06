#include "sapp/sounds/MidiFile.h"

#include <algorithm>
#include <fstream>

namespace sapp::sounds {
namespace {

struct Reader {
    const uint8_t* p;
    const uint8_t* end;
    bool ok = true;

    uint32_t u32() { if (end - p < 4) { ok = false; return 0; }
        uint32_t v = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
        p += 4; return v; }
    uint16_t u16() { if (end - p < 2) { ok = false; return 0; }
        uint16_t v = uint16_t((p[0] << 8) | p[1]); p += 2; return v; }
    uint8_t u8() { if (p >= end) { ok = false; return 0; } return *p++; }
    uint32_t varLen() {
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            const uint8_t b = u8();
            v = (v << 7) | (b & 0x7F);
            if ((b & 0x80) == 0) break;
        }
        return v;
    }
    void skip(uint32_t n) { if (uint32_t(end - p) < n) { ok = false; p = end; } else p += n; }
};

struct RawEvent {
    uint64_t tick;
    uint8_t status, channel, d1, d2;
};

struct TempoChange { uint64_t tick; uint32_t microsPerQuarter; };

} // namespace

MidiFileResult readMidiFile(const std::filesystem::path& path)
{
    MidiFileResult result;
    std::ifstream file(path, std::ios::binary);
    if (!file) { result.error = "cannot open file"; return result; }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
    if (bytes.size() < 14) { result.error = "not a MIDI file"; return result; }

    Reader r{bytes.data(), bytes.data() + bytes.size()};
    if (r.u32() != 0x4D546864 || r.u32() != 6) { result.error = "missing MThd"; return result; }
    const uint16_t format = r.u16();
    const uint16_t trackCount = r.u16();
    const uint16_t division = r.u16();
    if (format > 1) { result.error = "unsupported SMF format (only 0/1)"; return result; }
    if (division & 0x8000) { result.error = "SMPTE time division unsupported"; return result; }
    const double ticksPerQuarter = double(std::max<uint16_t>(1, division));

    std::vector<RawEvent> raw;
    std::vector<TempoChange> tempos{{0, 500000}};

    for (uint16_t t = 0; t < trackCount && r.ok; ++t) {
        if (r.u32() != 0x4D54726B) { result.error = "missing MTrk"; return result; }
        const uint32_t length = r.u32();
        const uint8_t* trackEnd = r.p + length;
        if (trackEnd > r.end) { result.error = "truncated track"; return result; }

        uint64_t tick = 0;
        uint8_t running = 0;
        while (r.ok && r.p < trackEnd) {
            tick += r.varLen();
            uint8_t status = r.u8();
            if (status < 0x80) { --r.p; status = running; if (status < 0x80) break; }
            else running = status;

            const uint8_t type = status & 0xF0;
            const uint8_t channel = status & 0x0F;
            if (type == 0x80 || type == 0x90 || type == 0xA0 || type == 0xB0 || type == 0xE0) {
                const uint8_t d1 = r.u8() & 0x7F;
                const uint8_t d2 = r.u8() & 0x7F;
                raw.push_back({tick, type, channel, d1, d2});
            } else if (type == 0xC0 || type == 0xD0) {
                r.u8();
            } else if (status == 0xFF) {
                const uint8_t meta = r.u8();
                const uint32_t len = r.varLen();
                if (meta == 0x51 && len == 3) {
                    const uint32_t micros = (uint32_t(r.u8()) << 16) | (uint32_t(r.u8()) << 8) | r.u8();
                    tempos.push_back({tick, micros});
                } else {
                    r.skip(len);
                }
            } else if (status == 0xF0 || status == 0xF7) {
                r.skip(r.varLen());
            } else {
                break;  // unknown status; abandon track safely
            }
        }
        r.p = trackEnd;
    }

    std::sort(tempos.begin(), tempos.end(), [](auto& a, auto& b) { return a.tick < b.tick; });
    std::stable_sort(raw.begin(), raw.end(), [](auto& a, auto& b) { return a.tick < b.tick; });

    // Tick → seconds with tempo map.
    auto tickToSeconds = [&](uint64_t tick) {
        double seconds = 0.0;
        uint64_t prevTick = 0;
        uint32_t micros = 500000;
        for (const auto& tc : tempos) {
            if (tc.tick >= tick) break;
            seconds += double(tc.tick - prevTick) / ticksPerQuarter * double(micros) * 1e-6;
            prevTick = tc.tick;
            micros = tc.microsPerQuarter;
        }
        seconds += double(tick - prevTick) / ticksPerQuarter * double(micros) * 1e-6;
        return seconds;
    };

    for (const auto& e : raw) {
        TimedMidiEvent out;
        out.seconds = tickToSeconds(e.tick);
        out.channel = e.channel;
        out.data1 = e.d1;
        out.data2 = e.d2;
        if (e.status == 0x90 && e.d2 == 0) out.status = 0x80;
        else out.status = e.status;
        if (e.status == 0xE0)
            out.bend14 = int16_t(((int(e.d2) << 7) | e.d1) - 8192);
        if (out.status == 0x80 || out.status == 0x90 || out.status == 0xB0 || out.status == 0xE0) {
            result.events.push_back(out);
            result.durationSeconds = std::max(result.durationSeconds, out.seconds);
        }
    }

    result.ok = true;
    return result;
}

} // namespace sapp::sounds
