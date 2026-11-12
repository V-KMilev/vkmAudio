#pragma once

// What the mix tests share: clips made by the test, and a mix rendered offline, 48 kHz stereo, into a
// buffer, with the ear at the origin looking down -Z.

#include <cmath>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include "vkm_audio/clip.h"
#include "vkm_audio/device.h"

#include "harness/check.h"

namespace Mixing {

constexpr uint32_t RATE     = 48000;
constexpr uint32_t CHANNELS = 2;

/// A clip of @p channels holding @p samples, interleaved, at @p rate.
inline Vkm::Audio::Clip clipOf(std::vector<int16_t> samples, uint32_t channels, uint32_t rate = RATE) {
    Vkm::Audio::Clip clip;
    clip.sampleRate = rate;
    clip.channels   = channels;
    clip.samples    = Vkm::Audio::ClipSamples(std::move(samples));
    return clip;
}

/// A mono sine of @p hz, @p seconds long, at @p amplitude of full scale.
inline Vkm::Audio::Clip tone(float hz, float seconds, float amplitude = 0.5f, uint32_t rate = RATE) {
    std::vector<int16_t> samples(static_cast<size_t>(seconds * static_cast<float>(rate)));
    for (size_t i = 0; i < samples.size(); ++i) {
        const float phase = 2.0f * glm::pi<float>() * hz * static_cast<float>(i) / static_cast<float>(rate);
        samples[i] = static_cast<int16_t>(std::lround(32767.0f * amplitude * std::sin(phase)));
    }
    return clipOf(std::move(samples), 1, rate);
}

/// A mono clip holding @p value throughout: its level in the mix is its gain, read off any sample.
inline Vkm::Audio::Clip steady(float seconds, int16_t value = 16384) {
    return clipOf(std::vector<int16_t>(static_cast<size_t>(seconds * RATE), value), 1);
}

/**
 * @brief Interleaved stereo frames out of the mixer.
 */
struct Block {
    std::vector<float> samples;

    size_t frames() const { return samples.size() / CHANNELS; }
    float  at(size_t frame, int channel) const { return samples[frame * CHANNELS + channel]; }

    /// The root mean square of @p channel, from @p first frame on.
    float rms(int channel, size_t first = 0) const {
        double sum = 0.0;
        for (size_t f = first; f < frames(); ++f) sum += static_cast<double>(at(f, channel)) * at(f, channel);
        return frames() > first ? static_cast<float>(std::sqrt(sum / double(frames() - first))) : 0.0f;
    }

    /// Both channels' root mean square together.
    float level(size_t first = 0) const {
        const float left  = rms(0, first);
        const float right = rms(1, first);
        return std::sqrt(0.5f * (left * left + right * right));
    }

    float peak() const {
        float most = 0.0f;
        for (const float s : samples) most = std::max(most, std::abs(s));
        return most;
    }

    bool finite() const {
        for (const float s : samples) {
            if (!std::isfinite(s)) return false;
        }
        return true;
    }
};

/// The next @p frames of @p device's mix.
inline Block render(Vkm::Audio::Device& device, size_t frames) {
    Block block;
    block.samples.assign(frames * CHANNELS, 0.0f);
    const uint64_t written = device.render(block.samples.data(), frames);
    CHECK(written == frames);
    return block;
}

/// An offline mixer with the ear at the origin, looking down -Z, Y up.
inline void openMix(Vkm::Audio::Device& device) {
    REQUIRE(device.openOffline(RATE, CHANNELS));
    device.setListener({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f});
}

/// A voice heard as it is: no position, no fade.
inline Vkm::Audio::VoiceParams flat(float volume = 1.0f) {
    Vkm::Audio::VoiceParams params;
    params.spatial = false;
    params.volume  = volume;
    return params;
}

/// A positioned voice at @p position, fading between @p nearDistance and @p farDistance.
inline Vkm::Audio::VoiceParams at(glm::vec3 position, float nearDistance = 1.0f, float farDistance = 50.0f) {
    Vkm::Audio::VoiceParams params;
    params.position    = position;
    params.minDistance = nearDistance;
    params.maxDistance = farDistance;
    return params;
}

/// The level of @p clip played once with @p params, after a block for the mixer to settle.
inline float levelOf(const Vkm::Audio::Clip& clip, const Vkm::Audio::VoiceParams& params) {
    Vkm::Audio::Device device;
    openMix(device);
    const Vkm::Audio::VoiceId voice = device.play(clip, params);
    REQUIRE(voice != 0u);
    render(device, 2048);
    return render(device, 4096).level();
}

} // namespace Mixing
