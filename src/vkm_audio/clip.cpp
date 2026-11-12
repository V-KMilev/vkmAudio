#include "vkm_audio/clip.h"

#include <utility>

namespace Vkm::Audio {

ClipSamples::ClipSamples(std::vector<int16_t> samples)
    : m_samples(std::make_shared<const std::vector<int16_t>>(std::move(samples)))
{
}

std::string findClipFault(const Clip& clip) {
    if (clip.channels == 0 || clip.channels > Clip::MAX_CHANNELS) {
        return std::to_string(clip.channels) + " channels is outside the 1.."
            + std::to_string(Clip::MAX_CHANNELS) + " a sound may have";
    }
    if (clip.sampleRate == 0 || clip.sampleRate > Clip::MAX_SAMPLE_RATE) {
        return "a sample rate of " + std::to_string(clip.sampleRate) + " is outside the 1.."
            + std::to_string(Clip::MAX_SAMPLE_RATE) + " a sound may have";
    }
    // The mixer reads whole frames, so a partial last frame runs it off the end of the buffer.
    if (clip.sampleCount() % clip.channels != 0) {
        return std::to_string(clip.sampleCount()) + " samples do not divide into "
            + std::to_string(clip.channels) + " channels";
    }
    return {};
}

} // namespace Vkm::Audio
