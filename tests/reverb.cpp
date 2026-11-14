// The reverb tank: a room's tail, run alone, with no mixer.

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "vkm_audio/reverb_tank.h"

#include "harness/check.h"

namespace {

using Vkm::Audio::ReverbTank;

constexpr uint32_t RATE  = 48000;
constexpr uint32_t BLOCK = 480;

/// @p seconds of @p tank's output, each channel alone, after a click on @p channel (every one when -1).
std::vector<std::vector<float>> tailOfClick(
    ReverbTank& tank,
    uint32_t channels,
    float seconds,
    int channel = -1
) {
    std::vector<std::vector<float>> tail(channels);
    std::vector<float>              in(BLOCK * channels, 0.0f);
    std::vector<float>              out(BLOCK * channels, 0.0f);
    for (uint32_t c = 0; c < channels; ++c) {
        if (channel < 0 || static_cast<int>(c) == channel) in[c] = 1.0f;
    }
    const uint32_t blocks = static_cast<uint32_t>(seconds * RATE) / BLOCK;
    for (uint32_t b = 0; b < blocks; ++b) {
        tank.process(in.data(), out.data(), BLOCK);
        std::fill(in.begin(), in.end(), 0.0f);
        for (uint32_t f = 0; f < BLOCK; ++f) {
            for (uint32_t c = 0; c < channels; ++c) tail[c].push_back(out[f * channels + c]);
        }
    }
    return tail;
}

/// The level of @p samples over 50 ms from @p seconds, in decibels.
float decibelsAt(const std::vector<float>& samples, float seconds) {
    const size_t first = static_cast<size_t>(seconds * RATE);
    const size_t count = RATE / 20;
    double sum = 0.0;
    const size_t last = std::min(first + count, samples.size());
    for (size_t i = first; i < last; ++i) sum += double(samples[i]) * samples[i];
    return 10.0f * static_cast<float>(std::log10(sum / static_cast<double>(count) + 1e-30));
}

std::vector<float> tailOf(float decay, float wet, float seconds = 1.0f) {
    ReverbTank tank;
    tank.prepare(RATE, 2);
    tank.setDecay(decay);
    tank.setWet(wet);
    return tailOfClick(tank, 2, seconds)[0];
}

} // namespace

TEST("a click's tail falls by 60 dB in the decay set") {
    for (const float decay : {0.5f, 1.0f, 2.0f}) {
        const std::vector<float> tail = tailOf(decay, 1.0f, 0.2f + decay + 0.1f);
        const float fall = decibelsAt(tail, 0.2f) - decibelsAt(tail, 0.2f + decay);
        CHECK_NEAR(fall, 60.0f, 6.0f);
    }
}

TEST("a level of zero, below it or not a number gives no tail, and a decay past its range is held to it") {
    const float notANumber = std::numeric_limits<float>::quiet_NaN();
    for (const float wet : {0.0f, -1.0f, notANumber, std::numeric_limits<float>::infinity()}) {
        float most = 0.0f;
        for (const float s : tailOf(1.0f, wet, 0.2f)) most = std::max(most, std::abs(s));
        CHECK(most == 0.0f);
    }
    // The level scales the tail and nothing else.
    const std::vector<float> full = tailOf(1.0f, 1.0f, 0.2f);
    const std::vector<float> half = tailOf(1.0f, 0.5f, 0.2f);
    float apart = 0.0f;
    for (size_t i = 0; i < full.size(); ++i) apart = std::max(apart, std::abs(half[i] - 0.5f * full[i]));
    CHECK(apart < 1e-6f);

    CHECK(tailOf(100.0f, 1.0f) == tailOf(10.0f, 1.0f));
    CHECK(tailOf(10.0f, 1.0f) != tailOf(5.0f, 1.0f));
    CHECK(tailOf(0.001f, 1.0f) == tailOf(0.1f, 1.0f));
    CHECK(tailOf(notANumber, 1.0f) == tailOf(0.1f, 1.0f));
    CHECK(tailOf(0.1f, 1.0f) != tailOf(0.2f, 1.0f));
}

TEST("a tail falls to silence and never through subnormal floats, which cost many times a normal one") {
    ReverbTank tank;
    tank.prepare(RATE, 2);
    tank.setDecay(0.5f);
    tank.setWet(1.0f);
    const auto tail = tailOfClick(tank, 2, 10.0f);

    size_t subnormal  = 0;
    float  lastSecond = 0.0f;
    for (const std::vector<float>& channel : tail) {
        for (size_t i = 0; i < channel.size(); ++i) {
            if (std::fpclassify(channel[i]) == FP_SUBNORMAL) ++subnormal;
            if (i + RATE >= channel.size()) lastSecond = std::max(lastSecond, std::abs(channel[i]));
        }
    }
    CHECK(subnormal == 0u);
    CHECK(lastSecond == 0.0f);
}

TEST("silence empties the tank within one block") {
    ReverbTank tank;
    tank.prepare(RATE, 2);
    tank.setDecay(5.0f);
    tank.setWet(1.0f);
    tailOfClick(tank, 2, 0.3f);

    std::vector<float> zeros(BLOCK * 2, 0.0f);
    std::vector<float> out(BLOCK * 2, 0.0f);
    tank.process(zeros.data(), out.data(), BLOCK);
    CHECK(std::abs(out[0]) + std::abs(out[1]) > 0.0f);

    tank.silence();
    tank.process(zeros.data(), out.data(), BLOCK);
    // Faded over the block, not cut: the last frame is silent, the first not yet.
    CHECK(out[2 * (BLOCK - 1)] == 0.0f);
    CHECK(std::abs(out[0]) > 0.0f);
    float after = 0.0f;
    for (int block = 0; block < 10; ++block) {
        tank.process(zeros.data(), out.data(), BLOCK);
        for (const float s : out) after = std::max(after, std::abs(s));
    }
    CHECK(after == 0.0f);
}

TEST("every input channel rings in every output channel") {
    for (const uint32_t channels : {2u, 3u}) {
        for (uint32_t in = 0; in < channels; ++in) {
            ReverbTank tank;
            tank.prepare(RATE, channels);
            tank.setDecay(1.0f);
            tank.setWet(1.0f);
            const auto tail = tailOfClick(tank, channels, 0.3f, static_cast<int>(in));
            for (uint32_t c = 0; c < channels; ++c) CHECK(decibelsAt(tail[c], 0.1f) > -60.0f);
        }
    }
}

TEST("a null input is silence going in, and the tail still rings out") {
    ReverbTank fed;
    ReverbTank unfed;
    for (ReverbTank* tank : {&fed, &unfed}) {
        tank->prepare(RATE, 2);
        tank->setDecay(1.0f);
        tank->setWet(1.0f);
    }
    std::vector<float> click(BLOCK * 2, 0.0f);
    click[0] = click[1] = 1.0f;
    std::vector<float> a(BLOCK * 2);
    std::vector<float> b(BLOCK * 2);
    fed.process(click.data(), a.data(), BLOCK);
    unfed.process(click.data(), b.data(), BLOCK);

    const std::vector<float> zeros(BLOCK * 2, 0.0f);
    float rang = 0.0f;
    for (int block = 0; block < 20; ++block) {
        fed.process(zeros.data(), a.data(), BLOCK);
        unfed.process(nullptr, b.data(), BLOCK);
        CHECK(a == b);
        for (const float s : b) rang = std::max(rang, std::abs(s));
    }
    CHECK(rang > 1e-3f);
}

TEST("a frame holding a sample that is not a number or infinite goes in as silence, and the tail rings on") {
    ReverbTank fed;
    ReverbTank clean;
    for (ReverbTank* tank : {&fed, &clean}) {
        tank->prepare(RATE, 2);
        tank->setDecay(1.0f);
        tank->setWet(1.0f);
    }
    std::vector<float> bad(BLOCK * 2, 0.0f);
    std::vector<float> silent(BLOCK * 2, 0.0f);
    bad[0] = bad[1] = silent[0] = silent[1] = 1.0f;

    bad[20] = std::numeric_limits<float>::quiet_NaN();
    bad[21] = 0.5f;
    bad[40] = std::numeric_limits<float>::infinity();
    bad[61] = -std::numeric_limits<float>::infinity();
    std::vector<float> a(BLOCK * 2);
    std::vector<float> b(BLOCK * 2);
    fed.process(bad.data(), a.data(), BLOCK);
    clean.process(silent.data(), b.data(), BLOCK);
    CHECK(a == b);

    const std::vector<float> zeros(BLOCK * 2, 0.0f);
    float rang = 0.0f;
    for (int block = 0; block < 20; ++block) {
        fed.process(zeros.data(), a.data(), BLOCK);
        clean.process(zeros.data(), b.data(), BLOCK);
        CHECK(a == b);
        for (const float s : a) rang = std::max(rang, std::abs(s));
    }
    CHECK(rang > 1e-3f);
}
