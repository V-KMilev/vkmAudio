#include "vkm_audio/decode.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "miniaudio.h"

namespace Vkm::Audio {

namespace {

// Frames pulled per read. Big enough that a three-minute track is a few hundred
// reads, small enough that the last (partial) one wastes nothing worth naming.
constexpr ma_uint64 DECODE_CHUNK_FRAMES = 16384;

// The most samples the declared length may reserve: ten minutes of stereo at 48 kHz. In samples, not
// frames, as the channel count comes out of the same header.
constexpr ma_uint64 MAX_RESERVE_SAMPLES = 10 * 60 * 48000 * 2;

/**
 * @brief Pull every frame the decoder has into @p samples.
 *
 * Driven by short reads rather than by a declared length: an mp3's frame count
 * is an estimate until it has been walked, and a decoder is allowed to answer
 * MA_NOT_IMPLEMENTED for the question entirely. The length is still asked for
 * first, as a reserve hint, so the common case allocates once.
 *
 * The hint is capped at MAX_RESERVE_SAMPLES because it comes out of the file's
 * own header, which a corrupt file can make claim any length; a longer clip still
 * loads whole, the cap bounding the hint and nothing else.
 */
bool decodeAll(ma_decoder& decoder, uint32_t channels, std::vector<int16_t>& samples) {
    ma_uint64 declaredFrames = 0;
    if (ma_decoder_get_length_in_pcm_frames(&decoder, &declaredFrames) == MA_SUCCESS) {
        const ma_uint64 frames = std::min<ma_uint64>(declaredFrames, MAX_RESERVE_SAMPLES / channels);
        samples.reserve(static_cast<size_t>(frames * channels));
    }

    std::vector<int16_t> chunk(static_cast<size_t>(DECODE_CHUNK_FRAMES * channels));
    for (;;) {
        ma_uint64 framesRead = 0;
        const ma_result result =
            ma_decoder_read_pcm_frames(&decoder, chunk.data(), DECODE_CHUNK_FRAMES, &framesRead);
        samples.insert(
            samples.end(),
            chunk.begin(),
            chunk.begin() + static_cast<size_t>(framesRead * channels)
        );

        if (result == MA_AT_END) return true;
        if (result != MA_SUCCESS) return false;
        // A successful read of nothing would spin forever; treat it as the end,
        // which is what it is for every decoder that reports one.
        if (framesRead == 0) return true;
    }
}

} // namespace

Decoded decodeFile(const std::string& path, Clip& out) {
    // Format is pinned to s16 - that is what a clip holds - while channels
    // and rate stay at 0, which asks the decoder for the file's own.
    ma_decoder_config config = ma_decoder_config_init(ma_format_s16, 0, 0);

    ma_decoder decoder;
    if (ma_decoder_init_file(path.c_str(), &config, &decoder) != MA_SUCCESS) return Decoded::Unreadable;

    out.channels   = decoder.outputChannels;
    out.sampleRate = decoder.outputSampleRate;

    std::vector<int16_t> samples;
    const bool decoded = decodeAll(decoder, out.channels, samples);
    ma_decoder_uninit(&decoder);

    if (!decoded || samples.empty()) return Decoded::Empty;
    out.samples = ClipSamples(std::move(samples));
    return Decoded::Whole;
}

} // namespace Vkm::Audio
