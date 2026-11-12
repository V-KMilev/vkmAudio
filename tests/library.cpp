// The library as a consumer sees it: its headers, and the lines it logs.

#include <sstream>
#include <string>

#include "vkm_log/logger.h"

#include "vkm_audio/acoustics.h"
#include "vkm_audio/clip.h"
#include "vkm_audio/decode.h"
#include "vkm_audio/device.h"
#include "vkm_audio/reverb_tank.h"

#include "harness/check.h"
#include "harness/scratch.h"

namespace {

// What reached this file through the library's target and its headers.
#if __has_include("miniaudio.h")
constexpr bool MINIAUDIO_REACHABLE = true;
#else
constexpr bool MINIAUDIO_REACHABLE = false;
#endif

#if defined(miniaudio_h) || defined(MA_NO_ENCODING) || defined(MA_NO_GENERATION) \
    || defined(MA_NO_RESOURCE_MANAGER)
constexpr bool MINIAUDIO_DEFINED = true;
#else
constexpr bool MINIAUDIO_DEFINED = false;
#endif

} // namespace

TEST("every public header compiles without miniaudio, whose header and definitions reach no consumer source") {
    CHECK(!MINIAUDIO_REACHABLE);
    CHECK(!MINIAUDIO_DEFINED);
}

TEST("a line the library logs carries VKM-AUDIO and its category, AUDIO") {
    Vkm::Audio::Device device;
    REQUIRE(device.openOffline(48000, 2));
    device.close();

    int found = 0;
    std::istringstream log(Vkm::Test::logText());
    for (std::string text; std::getline(log, text);) {
        const auto line = Vkm::Log::parseLine(text);
        REQUIRE(line.has_value());
        if (line->suffix == "VKM-TESTS") continue;
        ++found;
        CHECK(line->suffix == "VKM-AUDIO");
        CHECK(line->category == "AUDIO");
    }
    CHECK(found == 2);
}
