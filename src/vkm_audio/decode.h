#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "vkm_audio/clip.h"

namespace Vkm::Audio {

inline constexpr std::array<std::string_view, 3> DECODABLE_EXTENSIONS = {".wav", ".mp3", ".flac"};
    ///< The files decodeFile reads, by lowercased extension with its dot.

/**
 * @brief What decodeFile made of a file.
 */
enum class Decoded : uint8_t {
    Whole,       ///< Every frame the file holds, in the clip.
    Unreadable,  ///< Not a file any decoder opens: missing, or not wav, mp3 or flac.
    Empty,       ///< Opened, but decoded to nothing, or failed partway.
};

/**
 * @brief Decode a sound file whole into @p out.
 *
 * 16-bit interleaved PCM at the file's own sample rate and channel layout, so nothing is
 * resampled and the samples are the source material. Synchronous.
 *
 * @param path The file, as the OS opens it.
 * @param[out] out Takes the rate, the channels and the samples; untouched past the channels and
 *        rate unless the answer is Whole.
 * @return Whole, or why not.
 */
Decoded decodeFile(const std::string& path, Clip& out);

} // namespace Vkm::Audio
