// A clip and the files decoded into one: wav, mp3 and flac, written here byte by byte.

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#if !defined(_WIN32)
    #include <sys/resource.h>
#endif

// A sanitizer maps terabytes of shadow memory, which no address-space limit leaves room for.
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
    #define VKM_TEST_SANITIZED 1
#elif defined(__has_feature)
    #if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer) || __has_feature(memory_sanitizer)
        #define VKM_TEST_SANITIZED 1
    #endif
#endif

#include "vkm_audio/clip.h"
#include "vkm_audio/decode.h"

#include "harness/check.h"
#include "harness/scratch.h"

namespace {

namespace fs = std::filesystem;

using Vkm::Audio::Clip;
using Vkm::Audio::Decoded;
using Vkm::Audio::decodeFile;

void writeBytes(const fs::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream(path, std::ios::binary).write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size())
    );
}

void put(std::vector<uint8_t>& out, uint32_t value, int bytes) {
    for (int i = 0; i < bytes; ++i) out.push_back(static_cast<uint8_t>(value >> (8 * i)));
}

/// A 16-bit PCM wav of @p samples, interleaved.
std::vector<uint8_t> wav(uint32_t rate, uint16_t channels, const std::vector<int16_t>& samples) {
    const uint32_t data = static_cast<uint32_t>(samples.size() * 2);
    std::vector<uint8_t> out = {'R', 'I', 'F', 'F'};
    put(out, 36 + data, 4);
    for (const char c : std::string_view("WAVEfmt ")) out.push_back(static_cast<uint8_t>(c));
    put(out, 16, 4);
    put(out, 1, 2);
    put(out, channels, 2);
    put(out, rate, 4);
    put(out, rate * channels * 2u, 4);
    put(out, channels * 2u, 2);
    put(out, 16, 2);
    for (const char c : std::string_view("data")) out.push_back(static_cast<uint8_t>(c));
    put(out, data, 4);
    for (const int16_t s : samples) put(out, static_cast<uint16_t>(s), 2);
    return out;
}

/// @p frames MPEG-1 Layer III frames, mono at 32 kHz and 32 kbps, of silence: each 1152 samples.
std::vector<uint8_t> mp3(int frames) {
    std::vector<uint8_t> out;
    for (int f = 0; f < frames; ++f) {
        // The header: sync, MPEG-1, Layer III, no CRC; 32 kbps, 32 kHz, no padding; mono. Then side
        // information and main data of nothing, which is silence: 144 bytes in all.
        const uint8_t header[] = {0xFF, 0xFB, 0x18, 0xC0};
        out.insert(out.end(), header, header + 4);
        out.insert(out.end(), 140, 0);
    }
    return out;
}

/// The triangle FLAC_FILE holds: 600 frames, mono at 8 kHz, rising and falling by 256 every 32.
std::vector<int16_t> triangle() {
    std::vector<int16_t> samples(600);
    for (int i = 0; i < 600; ++i) {
        const int step = i % 64;
        samples[i] = static_cast<int16_t>((step < 32 ? step : 63 - step) * 256 - 4096);
    }
    return samples;
}

const std::vector<uint8_t> FLAC_FILE = {
    0x66, 0x4C, 0x61, 0x43, 0x80, 0x00, 0x00, 0x22, 0x02, 0x40, 0x02, 0x40, 0x00, 0x00, 0x1E, 0x00,
    0x00, 0xA5, 0x01, 0xF4, 0x00, 0xF0, 0x00, 0x00, 0x02, 0x58, 0x97, 0xF2, 0xD0, 0x7A, 0x72, 0xFB,
    0x2F, 0x1D, 0x6D, 0xC6, 0x8E, 0x9E, 0x46, 0x48, 0xDB, 0x47, 0xFF, 0xF8, 0x24, 0x08, 0x00, 0xCA,
    0x47, 0x01, 0xF0, 0xF1, 0xF2, 0xF3, 0xE6, 0x28, 0xA9, 0xAF, 0xB0, 0x56, 0x23, 0xCE, 0x90, 0x01,
    0xFF, 0xC9, 0x24, 0x92, 0x49, 0x24, 0x92, 0x4E, 0x64, 0x92, 0x49, 0x24, 0xFF, 0xFF, 0xE4, 0xCF,
    0xFF, 0x24, 0x92, 0x49, 0x24, 0x92, 0x49, 0x39, 0x92, 0x49, 0x24, 0x93, 0xFF, 0xFF, 0x93, 0x3F,
    0xFC, 0x92, 0x49, 0x24, 0x92, 0x49, 0x24, 0xE6, 0x49, 0x24, 0x92, 0x4F, 0xFF, 0xFE, 0x4C, 0xFF,
    0xF2, 0x49, 0x24, 0x92, 0x49, 0x24, 0x93, 0x99, 0x24, 0x92, 0x49, 0x3F, 0xFF, 0xF9, 0x33, 0xFF,
    0xC9, 0x24, 0x92, 0x49, 0x24, 0x92, 0x4E, 0x64, 0x92, 0x49, 0x24, 0xFF, 0xFF, 0xE4, 0xCF, 0xFF,
    0x24, 0x92, 0x49, 0x24, 0x92, 0x49, 0x39, 0x92, 0x49, 0x24, 0x93, 0xFF, 0xFF, 0x93, 0x3F, 0xFC,
    0x92, 0x49, 0x24, 0x92, 0x49, 0x24, 0xE6, 0x49, 0x24, 0x92, 0x4F, 0xFF, 0xFE, 0x4C, 0xFF, 0xF2,
    0x49, 0x24, 0x92, 0x49, 0x24, 0x93, 0x99, 0x24, 0x92, 0x49, 0x3F, 0xFF, 0xF9, 0x33, 0xFF, 0xC9,
    0x24, 0x92, 0x49, 0x24, 0x92, 0x4E, 0x64, 0x92, 0x49, 0x24, 0xFF, 0xFF, 0xE0, 0x62, 0x00, 0xFF,
    0xF8, 0x64, 0x08, 0x01, 0x17, 0x93, 0x47, 0x01, 0xF0, 0xF1, 0xF2, 0xF3, 0xE6, 0x27, 0x3D, 0xB6,
    0xF4, 0x40, 0x6B, 0xE5, 0xB8, 0x01, 0xFF, 0x92, 0x49, 0x24, 0x92, 0x29, 0x80,
};  ///< triangle() as an encoder writes it: STREAMINFO declaring its 600 frames, then two frames of it.

/// A file of the kind @p extension names, holding something to decode; empty for a kind this has none of.
std::vector<uint8_t> sampleFile(std::string_view extension) {
    if (extension == ".wav") return wav(22050, 2, {1, -2, 3, -4});
    if (extension == ".mp3") return mp3(10);
    if (extension == ".flac") return FLAC_FILE;
    return {};
}

} // namespace

TEST("findClipFault refuses a clip of 0 or 9 channels, a rate of 0 or past MAX_SAMPLE_RATE, or a part of a frame") {
    Clip good;
    good.sampleRate = 44100;
    good.channels   = 2;
    good.samples    = Vkm::Audio::ClipSamples(std::vector<int16_t>(10, 0));
    CHECK(Vkm::Audio::findClipFault(good) == "");

    Clip clip = good;
    clip.channels = 0;
    CHECK(Vkm::Audio::findClipFault(clip).find("0 channels") != std::string::npos);
    clip.channels = Clip::MAX_CHANNELS + 1;
    CHECK(Vkm::Audio::findClipFault(clip).find("9 channels") != std::string::npos);
    clip.channels = Clip::MAX_CHANNELS;
    CHECK(Vkm::Audio::findClipFault(clip).find("do not divide") != std::string::npos);

    clip            = good;
    clip.sampleRate = 0;
    CHECK(Vkm::Audio::findClipFault(clip).find("sample rate of 0") != std::string::npos);
    clip.sampleRate = Clip::MAX_SAMPLE_RATE + 1;
    CHECK(Vkm::Audio::findClipFault(clip).find("sample rate of 384001") != std::string::npos);
    clip.sampleRate = Clip::MAX_SAMPLE_RATE;
    CHECK(Vkm::Audio::findClipFault(clip) == "");

    clip         = good;
    clip.samples = Vkm::Audio::ClipSamples(std::vector<int16_t>(11, 0));
    CHECK(Vkm::Audio::findClipFault(clip) == "11 samples do not divide into 2 channels");
}

TEST("a clip's frames and duration follow from its samples, channels and rate") {
    Clip clip;
    CHECK(clip.frameCount() == 0u);
    CHECK(clip.duration() == 0.0f);
    clip.channels   = 2;
    clip.sampleRate = 8000;
    clip.samples    = Vkm::Audio::ClipSamples(std::vector<int16_t>(12000, 0));
    CHECK(clip.sampleCount() == 12000u);
    CHECK(clip.frameCount() == 6000u);
    CHECK(clip.duration() == 0.75f);
    clip.sampleRate = 0;
    CHECK(clip.duration() == 0.0f);
}

TEST("a wav decodes sample for sample, at its own rate and channel count") {
    std::vector<int16_t> samples;
    for (int i = 0; i < 3000; ++i) samples.push_back(static_cast<int16_t>((i * 977) % 65536 - 32768));
    const fs::path path = Vkm::Test::scratch() / "three.wav";
    writeBytes(path, wav(22050, 3, samples));

    Clip clip;
    REQUIRE(decodeFile(path.string(), clip) == Decoded::Whole);
    CHECK(clip.sampleRate == 22050u);
    CHECK(clip.channels == 3u);
    CHECK(clip.frameCount() == 1000u);
    REQUIRE(clip.samples);
    CHECK(*clip.samples == samples);
    CHECK(Vkm::Audio::findClipFault(clip) == "");
}

TEST("an mp3 and a flac decode to the length they declare, the flac sample for sample") {
    // Longer than one of the decoder's reads.
    const fs::path mp3Path = Vkm::Test::scratch() / "silence.mp3";
    writeBytes(mp3Path, mp3(20));
    Clip silence;
    REQUIRE(decodeFile(mp3Path.string(), silence) == Decoded::Whole);
    CHECK(silence.sampleRate == 32000u);
    CHECK(silence.channels == 1u);
    CHECK(silence.frameCount() == 20u * 1152u);
    bool quiet = true;
    for (const int16_t s : *silence.samples) quiet = quiet && s == 0;
    CHECK(quiet);

    const fs::path flacPath = Vkm::Test::scratch() / "triangle.flac";
    writeBytes(flacPath, FLAC_FILE);
    Clip clip;
    REQUIRE(decodeFile(flacPath.string(), clip) == Decoded::Whole);
    CHECK(clip.sampleRate == 8000u);
    CHECK(clip.channels == 1u);
    CHECK(*clip.samples == triangle());
}

TEST("a file no decoder reads is Unreadable, and one that decodes to nothing is Empty, the clip keeping its samples") {
    Clip clip;
    clip.channels   = 1;
    clip.sampleRate = 100;
    clip.samples    = Vkm::Audio::ClipSamples(std::vector<int16_t>{7, 8, 9});
    const auto kept = clip.samples.operator->();

    CHECK(decodeFile((Vkm::Test::scratch() / "missing.wav").string(), clip) == Decoded::Unreadable);
    const fs::path words = Vkm::Test::scratch() / "words.wav";
    Vkm::Test::writeText(words, "these are words, not a sound");
    CHECK(decodeFile(words.string(), clip) == Decoded::Unreadable);
    CHECK(clip.samples.operator->() == kept);

    const fs::path nothing = Vkm::Test::scratch() / "nothing.wav";
    writeBytes(nothing, wav(44100, 2, {}));
    CHECK(decodeFile(nothing.string(), clip) == Decoded::Empty);
    CHECK(clip.samples.operator->() == kept);
    CHECK(*clip.samples == std::vector<int16_t>{7, 8, 9});
}

TEST("a header declaring an absurd length reserves no more than its cap, and what is there still decodes") {
#if defined(_WIN32)
    Vkm::Test::skip("an address-space limit here needs setrlimit");
#elif defined(VKM_TEST_SANITIZED)
    Vkm::Test::skip("a sanitizer's shadow memory leaves no room for an address-space limit");
#else
    // Its STREAMINFO says 2^36 - 1 frames, half a terabyte of samples, though it holds 600.
    std::vector<uint8_t> absurd = FLAC_FILE;
    absurd[21] = static_cast<uint8_t>(absurd[21] | 0x0F);
    for (int i = 22; i < 26; ++i) absurd[i] = 0xFF;
    const fs::path path = Vkm::Test::scratch() / "absurd.flac";
    writeBytes(path, absurd);

    const fs::path decoded = Vkm::Test::scratch() / "decoded.txt";
    const auto ended = Vkm::Test::inChild([&] {
        // A gigabyte of address space: room for the cap's reserve, none for the header's.
        const rlimit limit{1u << 30, 1u << 30};
        setrlimit(RLIMIT_AS, &limit);
        Clip clip;
        const Decoded answer = decodeFile(path.string(), clip);
        const bool whole     = answer == Decoded::Whole && *clip.samples == triangle();
        Vkm::Test::writeText(decoded, std::to_string(whole) + " " + std::to_string(clip.frameCount()));
    });
    CHECK(ended == Vkm::Test::Ended::Returned);
    CHECK(Vkm::Test::readText(decoded) == "1 600");
#endif
}

TEST("a header of many channels declaring an absurd length reserves no more than its cap") {
#if defined(_WIN32)
    Vkm::Test::skip("an address-space limit here needs setrlimit");
#elif defined(VKM_TEST_SANITIZED)
    Vkm::Test::skip("a sanitizer's shadow memory leaves no room for an address-space limit");
#else
    // Its STREAMINFO says 2^36 - 1 frames of eight channels, though its frames hold 600 of one.
    std::vector<uint8_t> absurd = FLAC_FILE;
    absurd[20]                  = static_cast<uint8_t>(absurd[20] | (7u << 1));
    absurd[21]                  = static_cast<uint8_t>(absurd[21] | 0x0F);
    for (int i = 22; i < 26; ++i) absurd[i] = 0xFF;
    const fs::path path = Vkm::Test::scratch() / "absurd.flac";
    writeBytes(path, absurd);

    const auto ended = Vkm::Test::inChild([&] {
        // Room for the cap's reserve, none for eight channels of it in frames.
        const rlimit limit{384u << 20, 384u << 20};
        setrlimit(RLIMIT_AS, &limit);
        Clip clip;
        decodeFile(path.string(), clip);
    });
    CHECK(ended == Vkm::Test::Ended::Returned);
#endif
}

TEST("each listed extension is lowercase, starts with its dot, is listed once, and a file of its kind decodes") {
    const auto& listed = Vkm::Audio::DECODABLE_EXTENSIONS;
    for (size_t i = 0; i < listed.size(); ++i) {
        const std::string_view extension = listed[i];
        CHECK(extension.size() > 1u);
        CHECK(extension.front() == '.');
        bool lower = true;
        for (const char c : extension.substr(1)) lower = lower && !(c >= 'A' && c <= 'Z') && c != '.';
        CHECK(lower);
        for (size_t j = i + 1; j < listed.size(); ++j) CHECK(listed[j] != extension);

        const std::vector<uint8_t> bytes = sampleFile(extension);
        CHECK(!bytes.empty());
        const fs::path path = Vkm::Test::scratch() / ("sample" + std::string(extension));
        writeBytes(path, bytes);
        Clip clip;
        CHECK(decodeFile(path.string(), clip) == Decoded::Whole);
        CHECK(clip.frameCount() > 0u);
    }
}
