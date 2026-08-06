#include "sapp/sounds/InstrumentLoader.h"

#include <algorithm>
#include <atomic>
#include <map>
#include <thread>

#include "sapp/sounds/WavIo.h"

namespace sapp::sounds {
namespace {

// Case-tolerant existence check: many SFZ libraries are authored on Windows
// where "Samples/violin.WAV" and "samples/Violin.wav" both resolve.
std::filesystem::path resolveSamplePath(const std::filesystem::path& base,
                                        const std::string& relative)
{
    std::filesystem::path direct = base / relative;
    std::error_code ec;
    if (std::filesystem::exists(direct, ec)) return direct;

    // Walk components case-insensitively.
    std::filesystem::path cur = base;
    std::filesystem::path rel(relative);
    for (const auto& part : rel) {
        std::error_code ec2;
        if (std::filesystem::exists(cur / part, ec2)) { cur /= part; continue; }
        bool found = false;
        std::string want = part.string();
        std::transform(want.begin(), want.end(), want.begin(),
                       [](unsigned char c) { return char(std::tolower(c)); });
        std::error_code ec3;
        for (auto it = std::filesystem::directory_iterator(cur, ec3);
             !ec3 && it != std::filesystem::directory_iterator(); ++it) {
            std::string have = it->path().filename().string();
            std::transform(have.begin(), have.end(), have.begin(),
                           [](unsigned char c) { return char(std::tolower(c)); });
            if (have == want) { cur = it->path(); found = true; break; }
        }
        if (!found) return direct;  // give up; caller reports missing
    }
    return cur;
}

} // namespace

LoadResult InstrumentLoader::loadSfz(const std::filesystem::path& sfzPath) const
{
    SfzParser parser(options_.parserLimits);
    SfzParseResult parsed = parser.parseFile(sfzPath);

    LoadResult result;
    result.diagnostics = std::move(parsed.diagnostics);
    if (!parsed.ok) return result;

    return loadSamplesInternal(std::move(parsed.instrument), std::move(result.diagnostics));
}

LoadResult InstrumentLoader::loadSamples(InstrumentDefinition definition) const
{
    return loadSamplesInternal(std::move(definition), {});
}

LoadResult InstrumentLoader::loadSamplesInternal(InstrumentDefinition definition,
                                                 std::vector<Diagnostic> diagnostics) const
{
    LoadResult result;
    result.diagnostics = std::move(diagnostics);

    auto loaded = std::make_shared<LoadedInstrument>();

    const std::filesystem::path sourceDir =
        std::filesystem::path(definition.sourcePath).parent_path();
    const std::filesystem::path base =
        definition.defaultPath.empty() ? sourceDir : sourceDir / definition.defaultPath;

    // Deduplicate sample paths → one decode per file.
    std::map<std::string, SampleIndex> pathToIndex;
    std::vector<std::string> uniquePaths;
    for (auto& region : definition.regions) {
        auto it = pathToIndex.find(region.samplePath);
        if (it == pathToIndex.end()) {
            it = pathToIndex.emplace(region.samplePath, SampleIndex(uniquePaths.size())).first;
            uniquePaths.push_back(region.samplePath);
        }
        region.sample = it->second;
    }

    loaded->samples.resize(uniquePaths.size());
    std::vector<std::string> decodeErrors(uniquePaths.size());

    const int threads = std::clamp(options_.decodeThreads, 1, 32);
    std::atomic<size_t> next{0};
    auto worker = [&]() {
        for (;;) {
            const size_t i = next.fetch_add(1);
            if (i >= uniquePaths.size()) return;
            SampleData& sample = loaded->samples[i];
            sample.relativePath = uniquePaths[i];
            auto resolved = resolveSamplePath(base, uniquePaths[i]);
            std::error_code ec;
            if (!std::filesystem::exists(resolved, ec)) {
                // Libraries often reference .wav while shipping .flac (or the
                // reverse); retry with the sibling extension before giving up.
                std::filesystem::path alt(uniquePaths[i]);
                const std::string ext = alt.extension().string();
                if (ext == ".wav" || ext == ".WAV") alt.replace_extension(".flac");
                else if (ext == ".flac" || ext == ".FLAC") alt.replace_extension(".wav");
                resolved = resolveSamplePath(base, alt.string());
                std::error_code ec2;
                if (!std::filesystem::exists(resolved, ec2)) {
                    decodeErrors[i] = "missing";
                    continue;
                }
            }
            sample.resolvedPath = resolved.string();
            const auto decode = decodeAudioFile(resolved, sample);
            if (!decode.ok) decodeErrors[i] = decode.error;
        }
    };
    if (threads == 1 || uniquePaths.size() <= 1) {
        worker();
    } else {
        std::vector<std::thread> pool;
        for (int t = 0; t < threads; ++t) pool.emplace_back(worker);
        for (auto& t : pool) t.join();
    }

    // Report failures; mark affected regions as unplayable (sample = invalid).
    std::vector<bool> sampleOk(uniquePaths.size(), true);
    for (size_t i = 0; i < uniquePaths.size(); ++i) {
        if (decodeErrors[i].empty()) continue;
        sampleOk[i] = false;
        if (decodeErrors[i] == "missing") {
            result.missingSamples.push_back(uniquePaths[i]);
            result.diagnostics.push_back({Severity::Warning, uniquePaths[i], 0, "sample file not found"});
        } else {
            result.diagnostics.push_back({Severity::Warning, uniquePaths[i], 0,
                                          "sample decode failed: " + decodeErrors[i]});
        }
    }

    size_t playable = 0;
    for (auto& region : definition.regions) {
        if (region.sample >= 0 && !sampleOk[size_t(region.sample)])
            region.sample = kInvalidSample;
        if (region.sample != kInvalidSample) ++playable;
    }

    // An instrument with zero playable regions is not useful even in lenient
    // mode; fail with diagnostics intact.
    if (playable == 0 && !definition.regions.empty()) {
        result.diagnostics.push_back({Severity::Error, definition.sourcePath, 0,
                                      "no playable regions (all samples missing or undecodable)"});
        return result;
    }
    if (options_.failOnMissingSamples && !result.missingSamples.empty())
        return result;

    loaded->definition = std::move(definition);
    result.instrument = std::move(loaded);
    result.ok = true;
    return result;
}

} // namespace sapp::sounds
