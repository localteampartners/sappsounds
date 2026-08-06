#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <fstream>

#include "TestHelpers.h"
#include "sapp/sounds/WavIo.h"

using namespace sapp::sounds;
using Catch::Approx;

TEST_CASE("float32 WAV roundtrip", "[wavio]")
{
    const auto dir = sapptest::tempDir();
    const auto path = dir / "roundtrip.wav";

    auto src = sapptest::makeSine(440.0, 48000, 0.25, 2, 0.8f);
    REQUIRE(writeWavFile(path, src.data[0].data(), src.data[1].data(),
                         src.frames, 48000, true));

    SampleData decoded;
    auto result = decodeWavFile(path, decoded);
    REQUIRE(result.ok);
    CHECK(decoded.sampleRate == 48000);
    CHECK(decoded.channels == 2);
    CHECK(decoded.frames == src.frames);
    for (size_t i = 0; i < 100; ++i)
        CHECK(decoded.data[0][i] == Approx(src.data[0][i]).margin(1e-6));
    CHECK(decoded.peak == Approx(0.8f).margin(0.01));
}

TEST_CASE("16-bit PCM decode", "[wavio]")
{
    const auto dir = sapptest::tempDir();
    const auto path = dir / "pcm16.wav";
    auto src = sapptest::makeSine(440.0, 44100, 0.1, 1, 0.5f);
    REQUIRE(writeWavFile(path, src.data[0].data(), nullptr, src.frames, 44100, false));

    SampleData decoded;
    auto result = decodeWavFile(path, decoded);
    REQUIRE(result.ok);
    CHECK(decoded.sampleRate == 44100);
    CHECK(decoded.channels == 1);
    for (size_t i = 0; i < 100; ++i)
        CHECK(decoded.data[0][i] == Approx(src.data[0][i]).margin(1e-3));
}

TEST_CASE("malformed files fail without crashing", "[wavio]")
{
    const auto dir = sapptest::tempDir();

    SECTION("empty file")
    {
        const auto path = dir / "empty.wav";
        std::ofstream(path).close();
        SampleData s;
        CHECK_FALSE(decodeWavFile(path, s).ok);
    }
    SECTION("garbage bytes")
    {
        const auto path = dir / "garbage.wav";
        std::ofstream f(path, std::ios::binary);
        for (int i = 0; i < 2048; ++i) f.put(char(i * 37));
        f.close();
        SampleData s;
        CHECK_FALSE(decodeWavFile(path, s).ok);
    }
    SECTION("truncated data chunk is clamped, not fatal")
    {
        const auto good = dir / "good.wav";
        auto src = sapptest::makeSine(440.0, 48000, 0.1);
        REQUIRE(writeWavFile(good, src.data[0].data(), nullptr, src.frames, 48000, true));

        std::ifstream in(good, std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
        bytes.resize(bytes.size() / 2);
        const auto cut = dir / "truncated.wav";
        std::ofstream out(cut, std::ios::binary);
        out.write(bytes.data(), std::streamsize(bytes.size()));
        out.close();

        SampleData s;
        auto result = decodeWavFile(cut, s);
        CHECK(result.ok);
        CHECK(s.frames < src.frames);
    }
    SECTION("nonexistent file")
    {
        SampleData s;
        CHECK_FALSE(decodeWavFile(dir / "does-not-exist.wav", s).ok);
    }
}
