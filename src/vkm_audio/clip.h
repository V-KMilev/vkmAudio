#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Vkm::Audio {

/**
 * @brief A clip's interleaved PCM, shared with every voice playing it.
 *
 * Refcounted so a voice on the mixer's thread outlives its clip's owner freeing it. The filling
 * constructor is compiled into the library, not inline: the control block carries the code that
 * frees the samples, and a module its host unloads before the last voice lets go would take that
 * code with it.
 *
 * Immutable: nothing may edit a clip's samples out from under a voice.
 */
class ClipSamples {
    public:
        ClipSamples() = default;

        /**
         * @brief Take @p samples as a clip's buffer.
         *
         * @param samples Interleaved PCM, moved in.
         */
        explicit ClipSamples(std::vector<int16_t> samples);

        ~ClipSamples() = default;

        ClipSamples(const ClipSamples& other) = default;
        ClipSamples& operator=(const ClipSamples& other) = default;

        ClipSamples(ClipSamples && other) = default;
        ClipSamples& operator=(ClipSamples && other) = default;

    public:
        explicit operator bool() const noexcept { return m_samples != nullptr; }

        const std::vector<int16_t>& operator*() const { return *m_samples; }
        const std::vector<int16_t>* operator->() const { return m_samples.get(); }

    private:
        std::shared_ptr<const std::vector<int16_t>> m_samples;
};

/**
 * @brief A sound: fully decoded interleaved PCM, plus the rate and layout it was written at.
 */
struct Clip {
    static constexpr uint32_t MAX_CHANNELS = 8;  ///< Channel count past which a sound is refused as corrupt
                                                 ///< rather than read. A rejection threshold, not a
                                                 ///< capability: 7.1 is the outer edge of authored source
                                                 ///< material.

    static constexpr uint32_t MAX_SAMPLE_RATE = 384000;  ///< Sample rate past which a clip is refused as
                                                         ///< corrupt rather than read, well above any
                                                         ///< rate a sound is recorded at. The rate gives the
                                                         ///< clip's duration, so a wild one makes the length
                                                         ///< wrong everywhere it is read.

    uint32_t sampleRate = 0;  ///< Frames per second, as authored; the mixer resamples if it differs.
    uint32_t channels   = 0;  ///< 1 = mono, 2 = stereo, up to MAX_CHANNELS; a positioned voice wants a
                              ///< mono clip, as a stereo one only leans.

    ClipSamples samples;  ///< Interleaved PCM, shared with the voices playing it.

    /**
     * @brief Total samples across all channels.
     *
     * @return The count; 0 with no samples.
     */
    size_t sampleCount() const { return samples ? samples->size() : 0; }

    /**
     * @brief Length in sample frames (one frame is one sample per channel).
     *
     * @return The frame count; 0 for a clip with no channels.
     */
    uint64_t frameCount() const { return channels == 0 ? 0 : sampleCount() / channels; }

    /**
     * @brief Length in seconds.
     *
     * @return frameCount() over sampleRate; 0 when the clip carries no rate.
     */
    float duration() const {
        return sampleRate == 0 ? 0.0f : static_cast<float>(frameCount()) / static_cast<float>(sampleRate);
    }
};

/**
 * @brief Why @p clip cannot be played, or an empty string when it can.
 *
 * Device::play() refuses a clip with one: samples not in whole frames, or a rate or channel
 * count outside what a sound may have (MAX_SAMPLE_RATE, MAX_CHANNELS).
 *
 * @param clip Sound to judge.
 * @return The first fault found, worded to follow "cannot be played - ".
 */
std::string findClipFault(const Clip& clip);

} // namespace Vkm::Audio
