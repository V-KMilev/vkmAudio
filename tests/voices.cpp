// Voices: their ids, holds, cursors and stops, the cap on how many sound, and a device that is not there.

#include <cmath>
#include <limits>
#include <set>
#include <string>
#include <vector>

#include "vkm_audio/device.h"

#include "harness/check.h"
#include "harness/scratch.h"
#include "mixing.h"

using namespace Mixing;
using Vkm::Audio::Clip;
using Vkm::Audio::Device;
using Vkm::Audio::MAX_ACTIVE_VOICES;
using Vkm::Audio::MAX_HELD_VOICES;
using Vkm::Audio::VoiceId;
using Vkm::Audio::VoiceParams;

namespace {

int countOf(const std::string& text, const std::string& what) {
    int count = 0;
    for (size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + 1)) ++count;
    return count;
}

VoiceParams looping(int priority = 0, float volume = 1.0f) {
    VoiceParams params = flat(volume);
    params.loop        = true;
    params.priority    = priority;
    return params;
}

/// Fill the mixer to MAX_ACTIVE_VOICES with voices of @p clip: the first as @p first, the rest as @p rest.
std::vector<VoiceId> fill(
    Device& device,
    const Clip& clip,
    const std::vector<VoiceParams>& first,
    const VoiceParams& rest
) {
    std::vector<VoiceId> voices;
    for (size_t i = 0; i < MAX_ACTIVE_VOICES; ++i) {
        voices.push_back(device.play(clip, i < first.size() ? first[i] : rest));
        CHECK(voices.back() != 0u);
    }
    return voices;
}

} // namespace

TEST("play gives ids never used before, and nothing for an empty clip, a clip with no channels or a closed device") {
    Device     device;
    const Clip clip = tone(300.0f, 0.01f);
    CHECK(device.play(clip, flat()) == 0u);

    openMix(device);
    std::set<VoiceId> seen;
    for (int round = 0; round < 5; ++round) {
        for (int i = 0; i < 4; ++i) {
            const VoiceId voice = device.play(clip, flat());
            CHECK(voice != 0u);
            CHECK(seen.insert(voice).second);
        }
        // Played out and reaped: their ids are not handed out again.
        render(device, 2048);
        device.reapFinishedVoices();
        CHECK(device.voiceCount() == 0u);
    }

    CHECK(device.play(Clip{}, flat()) == 0u);
    Clip noChannels = clip;
    noChannels.channels = 0;
    CHECK(device.play(noChannels, flat()) == 0u);
    device.close();
    CHECK(device.play(clip, flat()) == 0u);
}

TEST("play gives no voice for a clip findClipFault refuses, and says why each time") {
    Device device;
    openMix(device);
    Clip noChannels = steady(0.1f);
    noChannels.channels = 0;
    const std::vector<Clip> faulty = {
        clipOf(std::vector<int16_t>(9 * 480, 1000), 9),
        clipOf(std::vector<int16_t>(254 * 48, 1000), 254),
        noChannels,
        clipOf(std::vector<int16_t>(480, 1000), 1, 0),
        clipOf(std::vector<int16_t>(480, 1000), 1, Clip::MAX_SAMPLE_RATE + 1),
        clipOf(std::vector<int16_t>(481, 1000), 2),
    };
    for (const Clip& clip : faulty) {
        const std::string fault = Vkm::Audio::findClipFault(clip);
        REQUIRE(!fault.empty());
        CHECK(device.play(clip, flat()) == 0u);
        CHECK(device.play(clip, flat()) == 0u);
        CHECK(countOf(Vkm::Test::logText(), "cannot be played - " + fault) == 2);
    }
    CHECK(device.voiceCount() == 0u);
    CHECK(device.play(steady(0.1f), flat()) != 0u);
}

TEST("a paused voice holds its cursor, silently, and resumes from it") {
    Device device;
    openMix(device);
    const VoiceId voice = device.play(steady(2.0f), flat());
    REQUIRE(voice != 0u);
    render(device, RATE / 4);
    // The mixer reads a little ahead of what it has handed out: within a hundredth of a second.
    CHECK_NEAR(device.voiceCursor(voice), 0.25f, 0.01f);

    device.pauseVoice(voice);
    CHECK(device.isVoicePaused(voice));
    CHECK(device.isVoiceActive(voice));
    render(device, 1024);
    const float held = device.voiceCursor(voice);
    // The pause ramps out over a few milliseconds.
    CHECK(held < 0.25f + 0.02f);
    const Block quiet = render(device, RATE / 4);
    CHECK(quiet.peak() == 0.0f);
    CHECK(device.voiceCursor(voice) == held);
    device.reapFinishedVoices();
    CHECK(device.voiceCount() == 1u);

    device.resumeVoice(voice);
    CHECK(!device.isVoicePaused(voice));
    const Block again = render(device, RATE / 4);
    CHECK(again.level(1024) > 0.1f);
    CHECK_NEAR(device.voiceCursor(voice), held + 0.25f, 0.01f);
}

TEST("a resume ramps up from wherever the pause's ramp got to, without a jump") {
    // Resumed before the mixer runs again, and halfway down the pause's 240 frames.
    for (const size_t into : {size_t{0}, size_t{120}}) {
        Device device;
        openMix(device);
        const VoiceId voice = device.play(steady(1.0f), looping());
        REQUIRE(voice != 0u);
        const float level = render(device, 2048).at(2047, 0);
        REQUIRE(level > 0.1f);

        device.pauseVoice(voice);
        const float last = into > 0 ? render(device, into).at(into - 1, 0) : level;
        device.resumeVoice(voice);
        const Block after = render(device, 2048);
        float jump = std::abs(after.at(0, 0) - last);
        for (size_t f = 1; f < after.frames(); ++f) {
            jump = std::max(jump, std::abs(after.at(f, 0) - after.at(f - 1, 0)));
        }
        CHECK(jump < level / 60.0f);
        CHECK_NEAR(after.at(2047, 0), level, 1e-6f);
    }
}

TEST("a one-shot that runs out while its pause ramps down stays ended when let go") {
    Device device;
    openMix(device);
    const Clip    clip = steady(0.1f);
    const VoiceId own  = device.play(clip, flat());
    const VoiceId bulk = device.play(clip, flat());
    REQUIRE(own != 0u);
    REQUIRE(bulk != 0u);
    // A hundred frames from the end, inside the pause's 240.
    render(device, clip.frameCount() - 100);
    device.pauseVoice(own);
    device.pauseAllVoices();
    render(device, 1024);

    device.resumeVoice(own);
    device.resumeAllVoices();
    CHECK(!device.isVoicePaused(own));
    CHECK(!device.isVoicePaused(bulk));
    CHECK(render(device, 2048).peak() == 0.0f);
    CHECK(!device.isVoiceActive(own));
    CHECK(!device.isVoiceActive(bulk));
    device.reapFinishedVoices();
    CHECK(device.voiceCount() == 0u);
}

TEST("pausing all holds only what sounds, and resuming all leaves a voice paused on purpose held") {
    Device device;
    openMix(device);
    const Clip clip    = steady(4.0f, 4000);
    const VoiceId kept = device.play(clip, looping());
    const VoiceId own  = device.play(clip, looping());
    const VoiceId both = device.play(clip, looping());
    REQUIRE(kept != 0u);
    REQUIRE(own != 0u);
    REQUIRE(both != 0u);
    device.pauseVoice(own);
    render(device, 1024);

    device.pauseAllVoices();
    CHECK(device.isVoicePaused(kept));
    // Held by the bulk pause, then by its own: only its own resume lets it go.
    device.pauseVoice(both);
    // One started afterwards plays.
    const VoiceId later = device.play(clip, looping());
    render(device, 1024);
    const float laterAlone = render(device, 2048).level();
    CHECK(laterAlone > 0.1f);

    device.resumeAllVoices();
    CHECK(!device.isVoicePaused(kept));
    CHECK(device.isVoicePaused(own));
    CHECK(device.isVoicePaused(both));
    CHECK(!device.isVoicePaused(later));
    render(device, 1024);
    CHECK_NEAR(render(device, 2048).level() / laterAlone, 2.0f, 0.02f);

    device.resumeVoice(both);
    device.resumeVoice(own);
    render(device, 1024);
    CHECK_NEAR(render(device, 2048).level() / laterAlone, 4.0f, 0.04f);
}

TEST("a seek past the end is held to the clip and finishes it, a negative or NaN one lands at the start, and the cursor reads a seek at once") {
    Device device;
    openMix(device);
    const Clip    clip  = steady(1.0f);
    const VoiceId voice = device.play(clip, flat());
    REQUIRE(voice != 0u);
    render(device, 4800);

    device.seekVoice(voice, 0.5f);
    CHECK_NEAR(device.voiceCursor(voice), 0.5f, 1e-4f);
    render(device, 4800);
    CHECK_NEAR(device.voiceCursor(voice), 0.6f, 0.01f);

    device.seekVoice(voice, -3.0f);
    CHECK(device.voiceCursor(voice) == 0.0f);
    render(device, 480);
    device.seekVoice(voice, std::numeric_limits<float>::quiet_NaN());
    CHECK(device.voiceCursor(voice) == 0.0f);
    render(device, 480);

    device.seekVoice(voice, 100.0f);
    CHECK(device.voiceCursor(voice) <= clip.duration());
    render(device, 1024);
    CHECK(!device.isVoiceActive(voice));
    device.reapFinishedVoices();
    CHECK(device.voiceCount() == 0u);

    // Further than a frame count holds.
    const VoiceId beyond = device.play(clip, flat());
    REQUIRE(beyond != 0u);
    device.seekVoice(beyond, 1e15f);
    CHECK(device.voiceCursor(beyond) <= clip.duration());
    render(device, 1024);
    CHECK(!device.isVoiceActive(beyond));
}

TEST("a stop ramps out over a few milliseconds without a jump, reads as gone at once, and the reap frees it after") {
    Device device;
    openMix(device);
    const VoiceId voice = device.play(steady(1.0f), looping());
    REQUIRE(voice != 0u);
    const Block before = render(device, 2048);
    const float level  = before.at(2047, 0);
    REQUIRE(level > 0.1f);

    device.stopVoice(voice);
    CHECK(!device.isVoiceActive(voice));
    CHECK(device.voiceCount() == 1u);

    const Block after = render(device, 2048);
    float jump = std::abs(after.at(0, 0) - level);
    size_t quiet = after.frames();
    for (size_t f = 1; f < after.frames(); ++f) {
        jump = std::max(jump, std::abs(after.at(f, 0) - after.at(f - 1, 0)));
        if (quiet == after.frames() && after.at(f, 0) == 0.0f) quiet = f;
    }
    // Five milliseconds at 48 kHz: 240 frames.
    CHECK(quiet > 120u);
    CHECK(quiet < 480u);
    CHECK(jump < level / 60.0f);

    device.reapFinishedVoices();
    CHECK(device.voiceCount() == 0u);
    // Stopping it again, or an id never given, does nothing.
    device.stopVoice(voice);
    device.stopVoice(9999);
    CHECK(device.voiceCount() == 0u);
}

TEST("a voice stopped while paused is held no more: resuming all leaves it stopped, and the reap frees it") {
    Device device;
    openMix(device);
    const Clip    clip = steady(1.0f);
    const VoiceId own  = device.play(clip, looping());
    const VoiceId bulk = device.play(clip, looping());
    REQUIRE(own != 0u);
    REQUIRE(bulk != 0u);
    device.pauseVoice(own);
    device.pauseAllVoices();
    render(device, 1024);

    device.stopVoice(own);
    device.stopVoice(bulk);
    device.resumeAllVoices();
    render(device, 1024);
    device.reapFinishedVoices();
    CHECK(device.voiceCount() == 0u);
}

TEST("at the voice cap a new voice takes the place of the lowest priority below its own") {
    Device device;
    openMix(device);
    const Clip clip   = steady(2.0f);
    const auto voices = fill(device, clip, {looping(1), looping(-2), looping(-1)}, looping(0));

    const VoiceId newer = device.play(clip, looping(0));
    CHECK(newer != 0u);
    CHECK(!device.isVoiceActive(voices[1]));
    CHECK(device.isVoiceActive(voices[2]));
    CHECK(device.isVoiceActive(voices[0]));

    // Nothing below priority -1 is left: the next at -1 takes no voice of its own rank as loud as it.
    CHECK(device.play(clip, looping(-1)) == 0u);
    CHECK(device.isVoiceActive(voices[2]));
}

TEST("at the voice cap, among its own priority a new voice takes the quietest quieter than it, then the oldest") {
    Device device;
    openMix(device);
    const Clip clip   = steady(2.0f);
    const auto voices = fill(device, clip, {looping(0, 0.2f), looping(0, 0.1f), looping(0, 0.1f)}, looping());

    CHECK(device.play(clip, looping(0, 0.5f)) != 0u);
    CHECK(device.isVoiceActive(voices[0]));
    CHECK(!device.isVoiceActive(voices[1]));
    CHECK(device.isVoiceActive(voices[2]));

    CHECK(device.play(clip, looping(0, 0.5f)) != 0u);
    CHECK(!device.isVoiceActive(voices[2]));
    CHECK(device.play(clip, looping(0, 0.5f)) != 0u);
    CHECK(!device.isVoiceActive(voices[0]));

    // None quieter is left: refused.
    CHECK(device.play(clip, looping(0, 0.5f)) == 0u);
}

TEST("at the voice cap with no listener, a positioned voice ranks as the silence it is heard as") {
    Device device;
    openMix(device);
    const Clip  clip   = steady(2.0f);
    VoiceParams placed = at({0.0f, 0.0f, -0.5f});
    placed.loop = true;
    const auto voices = fill(device, clip, {looping(0, 0.1f), placed}, looping());
    device.setListenerActive(false);

    // The flat voice is heard at a tenth, the positioned one not at all: it gives way first.
    CHECK(device.play(clip, looping(0, 0.5f)) != 0u);
    CHECK(device.isVoiceActive(voices[0]));
    CHECK(!device.isVoiceActive(voices[1]));
}

TEST("at the voice cap a held voice is never taken, and with none to take the new one is refused, said once a saturation") {
    Device device;
    openMix(device);
    const Clip clip   = steady(2.0f);
    const auto voices = fill(device, clip, {looping(-5)}, looping(0));
    device.pauseVoice(voices[0]);

    CHECK(device.play(clip, looping(0)) == 0u);
    CHECK(device.play(clip, looping(0)) == 0u);
    CHECK(device.isVoiceActive(voices[0]));
    CHECK(countOf(Vkm::Test::logText(), "voice budget") == 1);

    // Room again, then full again: said again.
    device.stopVoice(voices[5]);
    render(device, 1024);
    device.reapFinishedVoices();
    CHECK(device.play(clip, looping(0)) != 0u);
    CHECK(device.play(clip, looping(0)) == 0u);
    CHECK(countOf(Vkm::Test::logText(), "voice budget") == 2);
}

TEST("MAX_HELD_VOICES caps what the mixer holds, voices ramping out included, and a refusal is said once a saturation") {
    Device device;
    openMix(device);
    const Clip clip = steady(2.0f);
    fill(device, clip, {}, looping());
    device.stopAllVoices();
    for (size_t i = MAX_ACTIVE_VOICES; i < MAX_HELD_VOICES; ++i) CHECK(device.play(clip, looping()) != 0u);
    CHECK(device.voiceCount() == MAX_HELD_VOICES);
    // One that outranks a live voice would take its place under the active cap; the held cap refuses it.
    CHECK(device.play(clip, looping(5)) == 0u);
    CHECK(device.play(clip, looping(5)) == 0u);
    CHECK(countOf(Vkm::Test::logText(), "voices held") == 1);

    // The ramps end and the reap frees them, and it takes a place.
    render(device, 1024);
    device.reapFinishedVoices();
    CHECK(device.voiceCount() == MAX_HELD_VOICES - MAX_ACTIVE_VOICES);
    CHECK(device.play(clip, looping(5)) != 0u);

    // Full again: said again.
    device.stopAllVoices();
    for (size_t i = device.voiceCount(); i < MAX_HELD_VOICES; ++i) CHECK(device.play(clip, looping()) != 0u);
    CHECK(device.play(clip, looping(5)) == 0u);
    CHECK(countOf(Vkm::Test::logText(), "voices held") == 2);
}

TEST("a voice keeps its clip's samples alive after every other owner lets them go") {
    Device device;
    openMix(device);
    VoiceId voice = 0;
    {
        const Clip clip = steady(1.0f, 12000);
        voice = device.play(clip, looping());
        REQUIRE(voice != 0u);
    }
    // Blocks the freed samples would be reused for, filled otherwise.
    std::vector<std::vector<int16_t>> others;
    for (int i = 0; i < 8; ++i) others.emplace_back(static_cast<size_t>(RATE), static_cast<int16_t>(-30000));

    const Block block = render(device, 4096);
    CHECK_NEAR(block.at(4095, 0), 12000.0f / 32768.0f, 1e-4f);
    CHECK_NEAR(block.at(2000, 1), 12000.0f / 32768.0f, 1e-4f);
    CHECK(device.isVoiceActive(voice));
}

TEST("a device that is not open answers every call as nothing") {
    Device     device;
    const Clip clip = tone(300.0f, 0.1f);
    CHECK(!device.isOpen());
    CHECK(device.play(clip, flat()) == 0u);
    CHECK(!device.isVoiceActive(1));
    CHECK(!device.isVoicePaused(1));
    CHECK(device.voiceCount() == 0u);
    CHECK(device.voiceCursor(1) == 0.0f);
    device.updateVoice(1, flat());
    device.pauseVoice(1);
    device.resumeVoice(1);
    device.seekVoice(1, 0.5f);
    device.stopVoice(1);
    device.pauseAllVoices();
    device.resumeAllVoices();
    device.stopAllVoices();
    device.reapFinishedVoices();
    device.setListener({1.0f, 2.0f, 3.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f});
    device.setListenerActive(false);
    device.setRoomReverb(2.0f, 0.5f);
    device.close();
    std::vector<float> frames(64, 1.0f);
    CHECK(device.render(frames.data(), 32) == 0u);
    CHECK(frames[0] == 1.0f);

    // Opened and closed again, it is as it was.
    openMix(device);
    REQUIRE(device.play(clip, flat()) != 0u);
    device.close();
    CHECK(!device.isOpen());
    CHECK(device.voiceCount() == 0u);
    CHECK(device.play(clip, flat()) == 0u);
}
