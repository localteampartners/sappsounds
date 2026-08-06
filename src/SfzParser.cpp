#include "sapp/sounds/SfzParser.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>

namespace sapp::sounds {
namespace {

// ---------------------------------------------------------------- lexing ----

struct Token {
    enum class Kind { Header, Opcode } kind;
    std::string key;    // header name, or opcode key
    std::string value;  // opcode value (may contain spaces)
    std::string file;
    int line = 0;
};

struct LexContext {
    const SfzParserLimits& limits;
    std::vector<Token>& tokens;
    std::vector<Diagnostic>& diags;
    std::map<std::string, std::string> defines;
    std::set<std::string> includeStack;  // canonical paths, cycle detection
    bool failed = false;
};

void diag(std::vector<Diagnostic>& v, Severity s, const std::string& file, int line,
          std::string message)
{
    v.push_back({s, file, line, std::move(message)});
}

std::string applyDefines(const std::string& line, const std::map<std::string, std::string>& defines)
{
    if (defines.empty() || line.find('$') == std::string::npos) return line;
    std::string out = line;
    // Longest names first so $FOOBAR is not clobbered by $FOO.
    std::vector<const std::pair<const std::string, std::string>*> ordered;
    ordered.reserve(defines.size());
    for (const auto& kv : defines) ordered.push_back(&kv);
    std::sort(ordered.begin(), ordered.end(),
              [](auto* a, auto* b) { return a->first.size() > b->first.size(); });
    for (auto* kv : ordered) {
        size_t pos = 0;
        while ((pos = out.find(kv->first, pos)) != std::string::npos) {
            out.replace(pos, kv->first.size(), kv->second);
            pos += kv->second.size();
        }
    }
    return out;
}

std::string stripComment(const std::string& line)
{
    const size_t pos = line.find("//");
    return pos == std::string::npos ? line : line.substr(0, pos);
}

std::string trim(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(uint8_t(s[a]))) ++a;
    while (b > a && std::isspace(uint8_t(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

void lexLine(LexContext& ctx, const std::string& rawLine, const std::string& file, int lineNo)
{
    const std::string line = applyDefines(stripComment(rawLine), ctx.defines);

    size_t pos = 0;
    const size_t len = line.size();
    Token* openOpcode = nullptr;  // opcode still accepting value words

    while (pos < len) {
        while (pos < len && std::isspace(uint8_t(line[pos]))) ++pos;
        if (pos >= len) break;

        if (line[pos] == '<') {
            const size_t close = line.find('>', pos);
            if (close == std::string::npos) {
                diag(ctx.diags, Severity::Warning, file, lineNo, "unterminated header '<'");
                return;
            }
            ctx.tokens.push_back({Token::Kind::Header, line.substr(pos + 1, close - pos - 1), "", file, lineNo});
            openOpcode = nullptr;
            pos = close + 1;
            continue;
        }

        size_t wordEnd = pos;
        while (wordEnd < len && !std::isspace(uint8_t(line[wordEnd])) && line[wordEnd] != '<') ++wordEnd;
        std::string word = line.substr(pos, wordEnd - pos);
        pos = wordEnd;

        if (word.size() > ctx.limits.maxTokenLength) {
            diag(ctx.diags, Severity::Warning, file, lineNo, "token exceeds length cap, ignored");
            continue;
        }

        const size_t eq = word.find('=');
        if (eq != std::string::npos && eq > 0) {
            Token t{Token::Kind::Opcode, word.substr(0, eq), word.substr(eq + 1), file, lineNo};
            std::transform(t.key.begin(), t.key.end(), t.key.begin(),
                           [](unsigned char c) { return char(std::tolower(c)); });
            ctx.tokens.push_back(std::move(t));
            openOpcode = &ctx.tokens.back();
        } else if (openOpcode != nullptr) {
            // Continuation of a value containing spaces (e.g. sample paths).
            if (!openOpcode->value.empty()) openOpcode->value += ' ';
            openOpcode->value += word;
        } else {
            diag(ctx.diags, Severity::Warning, file, lineNo, "stray token '" + word + "'");
        }
    }
}

void lexFile(LexContext& ctx, const std::filesystem::path& path, int depth);

void lexText(LexContext& ctx, const std::string& text, const std::filesystem::path& baseDir,
             const std::string& displayName, int depth)
{
    std::istringstream stream(text);
    std::string raw;
    int lineNo = 0;
    while (std::getline(stream, raw)) {
        ++lineNo;
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        const std::string trimmed = trim(raw);

        if (trimmed.rfind("#include", 0) == 0) {
            // Defines may appear inside include paths: #include "$DIR/$DYN.txt"
            const std::string expanded = applyDefines(trimmed, ctx.defines);
            const size_t q1 = expanded.find('"');
            const size_t q2 = q1 == std::string::npos ? std::string::npos : expanded.find('"', q1 + 1);
            if (q1 == std::string::npos || q2 == std::string::npos) {
                diag(ctx.diags, Severity::Error, displayName, lineNo, "malformed #include");
                continue;
            }
            std::string inc = expanded.substr(q1 + 1, q2 - q1 - 1);
            std::replace(inc.begin(), inc.end(), '\\', '/');
            if (depth + 1 > ctx.limits.maxIncludeDepth) {
                diag(ctx.diags, Severity::Error, displayName, lineNo, "#include depth limit exceeded");
                ctx.failed = true;
                continue;
            }
            lexFile(ctx, baseDir / inc, depth + 1);
            continue;
        }
        if (trimmed.rfind("#define", 0) == 0) {
            std::istringstream ds(trimmed.substr(7));
            std::string name, value;
            ds >> name;
            std::getline(ds, value);
            value = trim(applyDefines(stripComment(value), ctx.defines));
            if (name.size() < 2 || name[0] != '$') {
                diag(ctx.diags, Severity::Warning, displayName, lineNo, "malformed #define, expected $NAME");
            } else {
                ctx.defines[name] = value;
            }
            continue;
        }
        lexLine(ctx, raw, displayName, lineNo);
    }
}

void lexFile(LexContext& ctx, const std::filesystem::path& path, int depth)
{
    std::error_code ec;
    const auto canonical = std::filesystem::weakly_canonical(path, ec);
    const std::string key = ec ? path.string() : canonical.string();
    if (ctx.includeStack.count(key) != 0) {
        diag(ctx.diags, Severity::Error, path.string(), 0, "recursive #include detected");
        ctx.failed = true;
        return;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        diag(ctx.diags, Severity::Error, path.string(), 0, "cannot open file");
        ctx.failed = true;
        return;
    }
    std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (text.size() > ctx.limits.maxFileBytes) {
        diag(ctx.diags, Severity::Error, path.string(), 0, "file exceeds size cap");
        ctx.failed = true;
        return;
    }

    ctx.includeStack.insert(key);
    lexText(ctx, text, path.parent_path(), path.filename().string(), depth);
    ctx.includeStack.erase(key);
}

// ------------------------------------------------------------- value parse --

std::optional<double> parseNumber(const std::string& v)
{
    if (v.empty()) return std::nullopt;
    char* endp = nullptr;
    const double d = std::strtod(v.c_str(), &endp);
    if (endp == v.c_str() || !std::isfinite(d)) return std::nullopt;
    return d;
}

// SFZ note names: c4 == 60. Accepts c, c#, db, case-insensitive, octave -1..9.
std::optional<int> parseNote(const std::string& raw)
{
    const std::string v = trim(raw);
    if (v.empty()) return std::nullopt;
    if (std::isdigit(uint8_t(v[0])) || v[0] == '-' || v[0] == '+') {
        auto n = parseNumber(v);
        if (!n) return std::nullopt;
        return int(std::lround(*n));
    }
    static const int base[7] = {9, 11, 0, 2, 4, 5, 7};  // a b c d e f g
    const char c = char(std::tolower(uint8_t(v[0])));
    if (c < 'a' || c > 'g') return std::nullopt;
    int semitone = base[c - 'a'];
    size_t i = 1;
    if (i < v.size() && (v[i] == '#' || v[i] == 's')) { ++semitone; ++i; }
    else if (i < v.size() && (v[i] == 'b' && (i + 1 < v.size()) &&
             (std::isdigit(uint8_t(v[i + 1])) || v[i + 1] == '-'))) { --semitone; ++i; }
    auto oct = parseNumber(v.substr(i));
    if (!oct) return std::nullopt;
    const int note = (int(std::lround(*oct)) + 1) * 12 + semitone;
    return note;
}

uint8_t clamp7(int v) { return uint8_t(std::clamp(v, 0, 127)); }

// ------------------------------------------------------------ region build --

struct OpcodeEntry {
    std::string value;
    std::string file;
    int line = 0;
};
using OpcodeMap = std::map<std::string, OpcodeEntry>;

struct BuildContext {
    InstrumentDefinition& inst;
    std::vector<Diagnostic>& diags;
    std::map<std::string, uint32_t> unsupported;
    uint32_t recognizedHits = 0;
};

bool applyOpcode(BuildContext& ctx, RegionDefinition& r, const std::string& key,
                 const OpcodeEntry& e)
{
    auto num = [&]() -> std::optional<double> {
        auto n = parseNumber(e.value);
        if (!n)
            diag(ctx.diags, Severity::Warning, e.file, e.line,
                 "invalid numeric value for '" + key + "': '" + e.value + "'");
        return n;
    };
    auto note = [&]() -> std::optional<int> {
        auto n = parseNote(e.value);
        if (!n)
            diag(ctx.diags, Severity::Warning, e.file, e.line,
                 "invalid note value for '" + key + "': '" + e.value + "'");
        return n;
    };

    if (key == "sample") {
        std::string p = e.value;
        std::replace(p.begin(), p.end(), '\\', '/');
        r.samplePath = p;
    }
    else if (key == "key") { if (auto n = note()) { r.loKey = r.hiKey = r.rootKey = clamp7(*n); } }
    else if (key == "lokey") { if (auto n = note()) r.loKey = clamp7(*n); }
    else if (key == "hikey") { if (auto n = note()) r.hiKey = clamp7(*n); }
    else if (key == "pitch_keycenter") { if (auto n = note()) r.rootKey = clamp7(*n); }
    else if (key == "transpose") { if (auto n = num()) r.transpose = std::clamp(int(std::lround(*n)), -64, 64); }
    else if (key == "tune") { if (auto n = num()) r.tuneCents = float(std::clamp(*n, -2400.0, 2400.0)); }
    else if (key == "lovel") { if (auto n = num()) r.loVel = clamp7(int(std::lround(*n))); }
    else if (key == "hivel") { if (auto n = num()) r.hiVel = clamp7(int(std::lround(*n))); }
    else if (key == "volume") { if (auto n = num()) r.volumeDb = float(std::clamp(*n, -144.0, 24.0)); }
    else if (key == "group_volume" || key == "global_volume" || key == "master_volume") {
        // ARIA scope volumes are additive with region volume.
        if (auto n = num()) r.extraVolumeDb += float(std::clamp(*n, -144.0, 24.0));
    }
    else if (key == "delay") {
        // Region start delay; folded into the envelope delay stage (during
        // which playback position is held, so attacks stay intact).
        if (auto n = num()) r.ampeg.delay = std::max(r.ampeg.delay, float(std::clamp(*n, 0.0, 100.0)));
    }
    else if (key == "pan") { if (auto n = num()) r.pan = float(std::clamp(*n, -100.0, 100.0)); }
    else if (key == "amp_veltrack") { if (auto n = num()) r.ampVeltrack = float(std::clamp(*n, -100.0, 100.0)); }
    else if (key == "offset") { if (auto n = num()) r.offset = std::max<int64_t>(0, int64_t(*n)); }
    else if (key == "end") { if (auto n = num()) r.end = int64_t(*n); }
    else if (key == "loop_mode" || key == "loopmode") {
        r.loop.explicitMode = true;
        if (e.value == "no_loop") r.loop.mode = LoopMode::NoLoop;
        else if (e.value == "one_shot") r.loop.mode = LoopMode::OneShot;
        else if (e.value == "loop_continuous") r.loop.mode = LoopMode::Continuous;
        else if (e.value == "loop_sustain") r.loop.mode = LoopMode::Sustain;
        else {
            r.loop.explicitMode = false;
            diag(ctx.diags, Severity::Warning, e.file, e.line, "unknown loop_mode '" + e.value + "'");
        }
    }
    else if (key == "loop_start" || key == "loopstart") { if (auto n = num()) r.loop.start = std::max<int64_t>(0, int64_t(*n)); }
    else if (key == "loop_end" || key == "loopend") { if (auto n = num()) r.loop.end = std::max<int64_t>(-1, int64_t(*n)); }
    else if (key == "loop_crossfade") {
        if (auto n = num()) r.loop.crossfadeSeconds = float(std::clamp(*n, 0.0, 4.0));
    }
    else if (key == "ampeg_delay") { if (auto n = num()) r.ampeg.delay = float(std::clamp(*n, 0.0, 100.0)); }
    else if (key == "ampeg_start") { if (auto n = num()) r.ampeg.start = float(std::clamp(*n, 0.0, 100.0) / 100.0); }
    else if (key == "ampeg_attack") { if (auto n = num()) r.ampeg.attack = float(std::clamp(*n, 0.0, 100.0)); }
    else if (key == "ampeg_hold") { if (auto n = num()) r.ampeg.hold = float(std::clamp(*n, 0.0, 100.0)); }
    else if (key == "ampeg_decay") { if (auto n = num()) r.ampeg.decay = float(std::clamp(*n, 0.0, 100.0)); }
    else if (key == "ampeg_sustain") { if (auto n = num()) r.ampeg.sustain = float(std::clamp(*n, 0.0, 100.0) / 100.0); }
    else if (key == "ampeg_release") { if (auto n = num()) r.ampeg.release = float(std::clamp(*n, 0.0, 100.0)); }
    else if (key == "trigger") {
        if (e.value == "attack") r.trigger = TriggerMode::Attack;
        else if (e.value == "release") r.trigger = TriggerMode::Release;
        else if (e.value == "release_key") r.trigger = TriggerMode::ReleaseKey;
        else if (e.value == "first") r.trigger = TriggerMode::First;
        else if (e.value == "legato") r.trigger = TriggerMode::Legato;
        else diag(ctx.diags, Severity::Warning, e.file, e.line, "unknown trigger '" + e.value + "'");
    }
    else if (key == "group") { if (auto n = num()) r.group = int32_t(*n); }
    else if (key == "off_by" || key == "offby") { if (auto n = num()) r.offBy = int32_t(*n); }
    else if (key == "off_mode") {
        if (e.value == "fast") r.offMode = OffMode::Fast;
        else if (e.value == "normal") r.offMode = OffMode::Normal;
        else if (e.value == "time") r.offMode = OffMode::Time;
        else diag(ctx.diags, Severity::Warning, e.file, e.line, "unknown off_mode '" + e.value + "'");
    }
    else if (key == "off_time") { if (auto n = num()) r.offTime = float(std::clamp(*n, 0.001, 4.0)); }
    else if (key == "note_polyphony" || key == "polyphony") {
        if (auto n = num()) r.notePolyphony = std::clamp(int(std::lround(*n)), 0, 256);
    }
    else if (key == "seq_length") { if (auto n = num()) r.seqLength = uint16_t(std::clamp(int(std::lround(*n)), 1, 128)); }
    else if (key == "seq_position") { if (auto n = num()) r.seqPosition = uint16_t(std::clamp(int(std::lround(*n)), 1, 128)); }
    else if (key == "lorand") { if (auto n = num()) r.loRand = float(std::clamp(*n, 0.0, 1.0)); }
    else if (key == "hirand") { if (auto n = num()) r.hiRand = float(std::clamp(*n, 0.0, 1.0)); }
    else if (key == "sw_lokey") { if (auto n = note()) r.swLoKey = std::clamp(*n, 0, 127); }
    else if (key == "sw_hikey") { if (auto n = note()) r.swHiKey = std::clamp(*n, 0, 127); }
    else if (key == "sw_last") { if (auto n = note()) r.swLast = std::clamp(*n, 0, 127); }
    else if (key == "sw_default") { if (auto n = note()) r.swDefault = std::clamp(*n, 0, 127); }
    else if (key == "sw_label") { r.swLabel = e.value; }
    else if (key == "xfin_lovel") { if (auto n = num()) r.xfinLoVel = int16_t(clamp7(int(std::lround(*n)))); }
    else if (key == "xfin_hivel") { if (auto n = num()) r.xfinHiVel = int16_t(clamp7(int(std::lround(*n)))); }
    else if (key == "xfout_lovel") { if (auto n = num()) r.xfoutLoVel = int16_t(clamp7(int(std::lround(*n)))); }
    else if (key == "xfout_hivel") { if (auto n = num()) r.xfoutHiVel = int16_t(clamp7(int(std::lround(*n)))); }
    else if (key == "xfin_lokey") { if (auto n = note()) r.xfinLoKey = int16_t(clamp7(*n)); }
    else if (key == "xfin_hikey") { if (auto n = note()) r.xfinHiKey = int16_t(clamp7(*n)); }
    else if (key == "xfout_lokey") { if (auto n = note()) r.xfoutLoKey = int16_t(clamp7(*n)); }
    else if (key == "xfout_hikey") { if (auto n = note()) r.xfoutHiKey = int16_t(clamp7(*n)); }
    else if (key == "xf_velcurve" || key == "xf_cccurve" || key == "xf_keycurve") {
        uint8_t curve = 0;
        if (e.value == "gain") curve = 1;
        else if (e.value != "power")
            diag(ctx.diags, Severity::Warning, e.file, e.line, "unknown crossfade curve '" + e.value + "'");
        if (key == "xf_velcurve") r.xfVelCurve = curve;
        else if (key == "xf_cccurve") r.xfCcCurve = curve;
        else r.xfKeyCurve = curve;
    }
    else if (key.rfind("xfin_locc", 0) == 0 || key.rfind("xfin_hicc", 0) == 0 ||
             key.rfind("xfout_locc", 0) == 0 || key.rfind("xfout_hicc", 0) == 0) {
        const bool in = key[2] == 'i';                       // xfIn vs xfOut
        const size_t numStart = in ? 9 : 10;
        const bool lo = key[numStart - 4] == 'l';            // lo.. vs hi..
        auto ccNum = parseNumber(key.substr(numStart));
        auto val = num();
        if (ccNum && val) {
            const uint8_t ccIndex = clamp7(int(std::lround(*ccNum)));
            const int16_t v = int16_t(clamp7(int(std::lround(*val))));
            auto it = std::find_if(r.ccCrossfades.begin(), r.ccCrossfades.end(),
                                   [ccIndex](const auto& c) { return c.cc == ccIndex; });
            if (it == r.ccCrossfades.end()) {
                r.ccCrossfades.push_back({ccIndex, -1, -1, -1, -1});
                it = r.ccCrossfades.end() - 1;
            }
            if (in) { if (lo) it->inLo = v; else it->inHi = v; }
            else    { if (lo) it->outLo = v; else it->outHi = v; }
        }
    }
    else if (key == "amp_random") { if (auto n = num()) r.ampRandomDb = float(std::clamp(*n, 0.0, 24.0)); }
    else if (key == "pitch_random") { if (auto n = num()) r.pitchRandomCents = float(std::clamp(*n, 0.0, 1200.0)); }
    else if (key == "delay_random") { if (auto n = num()) r.delayRandomSeconds = float(std::clamp(*n, 0.0, 4.0)); }
    else if (key.rfind("gain_cc", 0) == 0) {
        auto ccNum = parseNumber(key.substr(7));
        auto val = num();
        if (ccNum && val)
            r.gainCc.push_back({clamp7(int(std::lround(*ccNum))),
                                float(std::clamp(*val, -144.0, 48.0))});
    }
    else if (key.rfind("locc", 0) == 0 || key.rfind("hicc", 0) == 0) {
        auto ccNum = parseNumber(key.substr(4));
        auto val = num();
        if (ccNum && val) {
            const uint8_t cc = clamp7(int(std::lround(*ccNum)));
            const uint8_t v = clamp7(int(std::lround(*val)));
            auto it = std::find_if(r.ccConditions.begin(), r.ccConditions.end(),
                                   [cc](const CcCondition& c) { return c.cc == cc; });
            if (it == r.ccConditions.end()) {
                r.ccConditions.push_back({cc, 0, 127});
                it = r.ccConditions.end() - 1;
            }
            if (key[0] == 'l') it->lo = v; else it->hi = v;
        }
    }
    else {
        return false;  // unsupported
    }
    return true;
}

std::string noteName(int note)
{
    static const char* names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    if (note < 0 || note > 127) return "?";
    return std::string(names[note % 12]) + std::to_string(note / 12 - 1);
}

void finalizeInstrument(BuildContext& ctx)
{
    auto& inst = ctx.inst;

    // Playable range + keyswitch union (attack regions only).
    for (const auto& r : inst.regions) {
        if (r.trigger == TriggerMode::Attack || r.trigger == TriggerMode::First ||
            r.trigger == TriggerMode::Legato) {
            inst.loKeyUsed = std::min(inst.loKeyUsed, r.loKey);
            inst.hiKeyUsed = std::max(inst.hiKeyUsed, r.hiKey);
        }
        if (r.swLoKey >= 0) {
            inst.keyswitchLo = inst.keyswitchLo < 0 ? r.swLoKey : std::min(inst.keyswitchLo, r.swLoKey);
        }
        if (r.swHiKey >= 0) {
            inst.keyswitchHi = inst.keyswitchHi < 0 ? r.swHiKey : std::max(inst.keyswitchHi, r.swHiKey);
        }
        if (r.swDefault >= 0 && inst.defaultKeyswitch < 0)
            inst.defaultKeyswitch = r.swDefault;
    }
    if (inst.loKeyUsed > inst.hiKeyUsed) { inst.loKeyUsed = 0; inst.hiKeyUsed = 127; }

    // Articulations from keyswitch structure.
    std::map<int, ArticulationInfo> byKeyswitch;
    uint32_t alwaysOn = 0;
    for (const auto& r : inst.regions) {
        if (r.trigger == TriggerMode::Release || r.trigger == TriggerMode::ReleaseKey) continue;
        if (r.swLast >= 0) {
            auto& a = byKeyswitch[r.swLast];
            a.keyswitch = r.swLast;
            ++a.regionCount;
            if (a.name.empty() && !r.swLabel.empty()) a.name = r.swLabel;
        } else {
            ++alwaysOn;
        }
    }
    for (auto& [ks, a] : byKeyswitch) {
        if (a.name.empty()) a.name = "Keyswitch " + noteName(ks);
        a.isDefault = (ks == inst.defaultKeyswitch);
        inst.articulations.push_back(a);
    }
    if (inst.articulations.empty() && alwaysOn > 0)
        inst.articulations.push_back({"Default", -1, alwaysOn, true});
    else if (!inst.articulations.empty() && inst.defaultKeyswitch < 0 && !byKeyswitch.empty()) {
        inst.defaultKeyswitch = byKeyswitch.begin()->first;
        inst.articulations.front().isDefault = true;
    }

    for (const auto& [name, count] : ctx.unsupported) {
        inst.unsupportedOpcodes.push_back(name);
        inst.unsupportedOpcodeHits += count;
    }
    inst.recognizedOpcodeHits = ctx.recognizedHits;
}

SfzParseResult parseTokens(std::vector<Token> tokens, std::vector<Diagnostic> diags,
                           bool lexFailed, const std::filesystem::path& baseDir,
                           const std::string& name, const SfzParserLimits& limits)
{
    SfzParseResult result;
    result.diagnostics = std::move(diags);
    result.instrument.name = name;

    BuildContext ctx{result.instrument, result.diagnostics, {}, 0};

    // Single pass. Inheritance snapshots (global ← master ← group ← region)
    // are captured when each <region> is flushed, i.e. with the scopes that
    // were active at that point in the token stream.
    OpcodeMap control, global, master, group, pendingRegion;
    OpcodeMap* scope = &global;
    std::vector<std::pair<OpcodeMap, uint32_t>> finals;  // merged map + source line
    uint32_t pendingLine = 0;
    bool havePending = false;

    auto flushRegion = [&]() {
        if (!havePending) return;
        OpcodeMap merged = global;
        for (const auto& kv : master) merged[kv.first] = kv.second;
        for (const auto& kv : group) merged[kv.first] = kv.second;
        for (const auto& kv : pendingRegion) merged[kv.first] = kv.second;
        finals.emplace_back(std::move(merged), pendingLine);
        pendingRegion.clear();
        havePending = false;
    };

    for (auto& t : tokens) {
        if (t.kind == Token::Kind::Header) {
            std::string h = t.key;
            std::transform(h.begin(), h.end(), h.begin(),
                           [](unsigned char c) { return char(std::tolower(c)); });
            flushRegion();
            if (h == "control") scope = &control;
            else if (h == "global") scope = &global;
            else if (h == "master") { master.clear(); group.clear(); scope = &master; }
            else if (h == "group") { group.clear(); scope = &group; }
            else if (h == "region") {
                if (finals.size() >= limits.maxRegions) {
                    result.diagnostics.push_back({Severity::Error, t.file, t.line, "region count cap exceeded"});
                    scope = nullptr;
                    break;
                }
                havePending = true;
                pendingLine = uint32_t(t.line);
                scope = &pendingRegion;
            } else {
                result.diagnostics.push_back({Severity::Warning, t.file, t.line, "unknown header <" + t.key + ">"});
                scope = nullptr;
            }
            continue;
        }
        if (scope != nullptr) (*scope)[t.key] = {t.value, t.file, t.line};
    }
    flushRegion();

    // <control> opcodes.
    for (const auto& [key, entry] : control) {
        if (key == "default_path") {
            std::string p = entry.value;
            std::replace(p.begin(), p.end(), '\\', '/');
            result.instrument.defaultPath = p;
            ++ctx.recognizedHits;
        } else if (key.rfind("set_cc", 0) == 0) {
            auto cc = parseNumber(key.substr(6));
            auto v = parseNumber(entry.value);
            if (cc && v)
                result.instrument.controlDefaults.push_back(
                    {clamp7(int(std::lround(*cc))), clamp7(int(std::lround(*v)))});
            ++ctx.recognizedHits;
        } else if (key.rfind("label_cc", 0) == 0) {
            auto cc = parseNumber(key.substr(8));
            if (cc)
                result.instrument.controlLabels.push_back({clamp7(int(std::lround(*cc))), entry.value});
            ++ctx.recognizedHits;
        } else {
            ++ctx.unsupported[key];
        }
    }

    // Build regions from merged opcode maps.
    for (auto& [merged, line] : finals) {
        RegionDefinition r;
        r.sourceLine = line;
        for (const auto& [key, entry] : merged) {
            if (applyOpcode(ctx, r, key, entry)) ++ctx.recognizedHits;
            else ++ctx.unsupported[key];
        }
        if (r.samplePath.empty()) {
            result.diagnostics.push_back({Severity::Warning, result.instrument.sourcePath,
                                          int(line), "region without sample= ignored"});
            continue;
        }
        if (r.loKey > r.hiKey) std::swap(r.loKey, r.hiKey);
        if (r.loVel > r.hiVel) std::swap(r.loVel, r.hiVel);
        if (r.seqPosition > r.seqLength) r.seqPosition = r.seqLength;
        result.instrument.regions.push_back(std::move(r));
    }

    finalizeInstrument(ctx);
    result.ok = !lexFailed;
    (void)baseDir;
    return result;
}

} // namespace

SfzParseResult SfzParser::parseFile(const std::filesystem::path& path) const
{
    std::vector<Token> tokens;
    std::vector<Diagnostic> diags;
    LexContext ctx{limits_, tokens, diags, {}, {}, false};
    lexFile(ctx, path, 0);

    auto result = parseTokens(std::move(tokens), std::move(diags), ctx.failed,
                              path.parent_path(), path.stem().string(), limits_);
    result.instrument.sourcePath = std::filesystem::absolute(path).string();
    return result;
}

SfzParseResult SfzParser::parseString(const std::string& text,
                                      const std::filesystem::path& baseDir,
                                      const std::string& displayName) const
{
    std::vector<Token> tokens;
    std::vector<Diagnostic> diags;
    LexContext ctx{limits_, tokens, diags, {}, {}, false};
    lexText(ctx, text, baseDir, displayName, 0);

    auto result = parseTokens(std::move(tokens), std::move(diags), ctx.failed,
                              baseDir, displayName, limits_);
    result.instrument.sourcePath = (baseDir / displayName).string();
    return result;
}

} // namespace sapp::sounds
