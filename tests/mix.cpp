// The mix: what the ear hears of a voice, where it stands, and the room it rings in.

#include <cmath>
#include <limits>
#include <string>
#include <vector>

#if !defined(_WIN32)
    #include <unistd.h>
#endif

#include "vkm_audio/acoustics.h"
#include "vkm_audio/device.h"

#include "harness/check.h"
#include "harness/scratch.h"
#include "mixing.h"

using namespace Mixing;
using Vkm::Audio::Device;
using Vkm::Audio::Rolloff;
using Vkm::Audio::VoiceParams;

namespace {

constexpr float NOT_A_NUMBER = std::numeric_limits<float>::quiet_NaN();
constexpr float INFINITE     = std::numeric_limits<float>::infinity();

} // namespace

TEST("an offline mix of one voice makes sound, of none silence, and says how many frames it wrote") {
    Device device;
    std::vector<float> frames(256 * CHANNELS, 1.0f);
    CHECK(device.render(frames.data(), 256) == 0u);

    openMix(device);
    const Block quiet = render(device, 4800);
    CHECK(quiet.peak() == 0.0f);

    REQUIRE(device.play(tone(440.0f, 1.0f), flat()) != 0u);
    const Block heard = render(device, 4800);
    CHECK(heard.rms(0) > 0.2f);
    CHECK(heard.rms(1) > 0.2f);
    CHECK(device.render(nullptr, 16) == 0u);
}

TEST("a positioned mono voice is louder in the ear it is beside, and crosses over as it passes the listener") {
    Device device;
    openMix(device);
    VoiceParams params = at({8.0f, 0.0f, -1.0f});
    const auto  voice  = device.play(tone(300.0f, 2.0f), params);
    REQUIRE(voice != 0u);

    // From the right, across the front, to the left: the right ear's share falls all the way.
    float lastShare = 2.0f;
    int rising = 0;
    float first = 0.0f;
    for (float x = 8.0f; x >= -8.0f; x -= 2.0f) {
        params.position.x = x;
        device.updateVoice(voice, params);
        render(device, 1024);
        const Block block = render(device, 2048);
        const float share = block.rms(1) / (block.rms(0) + block.rms(1));
        if (x == 8.0f) first = share;
        if (share > lastShare + 1e-4f) ++rising;
        lastShare = share;
    }
    CHECK(first > 0.7f);
    CHECK(lastShare < 0.3f);
    CHECK(rising == 0);
}

TEST("a stereo clip leans as it moves, but its right channel is never heard from the left") {
    // Silence on the left, a tone on the right.
    std::vector<int16_t> samples(2 * RATE);
    for (size_t f = 0; f < RATE; ++f) {
        const float phase = 2.0f * glm::pi<float>() * 500.0f * static_cast<float>(f) / RATE;
        samples[2 * f + 1] = static_cast<int16_t>(12000.0f * std::sin(phase));
    }
    Device device;
    openMix(device);
    VoiceParams params = at({-6.0f, 0.0f, -1.0f});
    params.loop = true;
    const auto voice = device.play(clipOf(samples, 2), params);
    REQUIRE(voice != 0u);

    float leftHeard = 0.0f;
    float quietest  = 1.0f;
    float loudest   = 0.0f;
    for (const float x : {-6.0f, -2.0f, 0.0f, 2.0f, 6.0f}) {
        params.position.x = x;
        device.updateVoice(voice, params);
        const Block block = render(device, 4096);
        leftHeard = std::max(leftHeard, block.rms(0));
        quietest  = std::min(quietest, block.rms(1, 1024));
        loudest   = std::max(loudest, block.rms(1, 1024));
    }
    CHECK(leftHeard == 0.0f);
    CHECK(quietest > 0.0f);
    CHECK(loudest > quietest * 1.2f);
}

TEST("linear rolloff: full volume inside the near distance, a straight line to silence at the far one") {
    const Vkm::Audio::Clip clip = steady(1.0f);
    const float full = levelOf(clip, at({0.0f, 0.0f, -2.0f}, 2.0f, 10.0f));
    REQUIRE(full > 0.1f);
    CHECK_NEAR(levelOf(clip, at({0.0f, 0.0f, -0.5f}, 2.0f, 10.0f)) / full, 1.0f, 0.01f);
    CHECK_NEAR(levelOf(clip, at({0.0f, 0.0f, -4.0f}, 2.0f, 10.0f)) / full, 0.75f, 0.01f);
    CHECK_NEAR(levelOf(clip, at({0.0f, 0.0f, -6.0f}, 2.0f, 10.0f)) / full, 0.5f, 0.01f);
    CHECK_NEAR(levelOf(clip, at({0.0f, 0.0f, -8.0f}, 2.0f, 10.0f)) / full, 0.25f, 0.01f);
    CHECK(levelOf(clip, at({0.0f, 0.0f, -10.0f}, 2.0f, 10.0f)) < full * 1e-3f);
    CHECK(levelOf(clip, at({0.0f, 0.0f, -30.0f}, 2.0f, 10.0f)) < full * 1e-3f);
}

TEST("inverse rolloff fades as inverseRolloffGain says, in the mix too, and follows the ear as it moves") {
    VoiceParams params = at({0.0f, 0.0f, -1.0f}, 1.0f, 11.0f);
    params.rolloff = Rolloff::Inverse;
    CHECK(Vkm::Audio::inverseRolloffGain(params, 0.5f) == 1.0f);
    CHECK(Vkm::Audio::inverseRolloffGain(params, 1.0f) == 1.0f);
    const auto bent = [](float nearDistance, float distance, float farDistance) {
        return (nearDistance / distance - nearDistance / farDistance) / (1.0f - nearDistance / farDistance);
    };
    CHECK_NEAR(Vkm::Audio::inverseRolloffGain(params, 2.0f), bent(1.0f, 2.0f, 11.0f), 1e-6f);
    CHECK_NEAR(Vkm::Audio::inverseRolloffGain(params, 5.0f), bent(1.0f, 5.0f, 11.0f), 1e-6f);
    CHECK(Vkm::Audio::inverseRolloffGain(params, 11.0f) == 0.0f);
    CHECK(Vkm::Audio::inverseRolloffGain(params, 40.0f) == 0.0f);

    // A near distance under the floor is held to it.
    VoiceParams close = params;
    close.minDistance = 0.001f;
    const float floor = Vkm::Audio::INVERSE_ROLLOFF_MIN_DISTANCE;
    CHECK(Vkm::Audio::inverseRolloffGain(close, floor) == 1.0f);
    CHECK_NEAR(Vkm::Audio::inverseRolloffGain(close, 2.0f * floor), bent(floor, 2.0f * floor, 11.0f), 1e-6f);

    // A range with no far is not faded.
    VoiceParams endless = params;
    endless.maxDistance = 0.5f;
    CHECK(Vkm::Audio::inverseRolloffGain(endless, 100.0f) == 1.0f);

    // In the mix: straight ahead at each distance, against the voice inside its near distance.
    const Vkm::Audio::Clip clip = steady(1.0f);
    const float full = levelOf(clip, params);
    REQUIRE(full > 0.1f);
    for (const float distance : {2.0f, 5.0f, 9.0f}) {
        VoiceParams away = params;
        away.position = {0.0f, 0.0f, -distance};
        CHECK_NEAR(levelOf(clip, away) / full, Vkm::Audio::inverseRolloffGain(params, distance), 0.01f);
    }

    // The ear walks off from a voice nothing updates.
    Device device;
    openMix(device);
    REQUIRE(device.play(clip, params) != 0u);
    render(device, 2048);
    const float atStart = render(device, 2048).level();
    device.setListener({0.0f, 0.0f, 4.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f});
    render(device, 2048);
    CHECK_NEAR(render(device, 2048).level() / atStart, Vkm::Audio::inverseRolloffGain(params, 5.0f), 0.01f);
}

TEST("with no listener a positioned voice is silent and a flat one plays on") {
    Device device;
    openMix(device);
    device.setListenerActive(false);
    REQUIRE(device.play(steady(1.0f), at({0.0f, 0.0f, -2.0f})) != 0u);
    render(device, 2048);
    CHECK(render(device, 2048).peak() == 0.0f);

    REQUIRE(device.play(steady(1.0f), flat()) != 0u);
    render(device, 1024);
    CHECK(render(device, 2048).level() > 0.1f);

    // An ear again, and the positioned one is heard beside it.
    const float flatOnly = render(device, 2048).level();
    device.setListenerActive(true);
    render(device, 2048);
    CHECK(render(device, 2048).level() > flatOnly * 1.2f);
}

TEST("a negative, infinite or NaN gain, distance or occlusion is heard as silence or nothing, never as a broken mix") {
    for (const float bad : {-1.0f, INFINITE, -INFINITE, NOT_A_NUMBER}) {
        Device device;
        openMix(device);
        VoiceParams loud = flat();
        loud.volume = bad;
        REQUIRE(device.play(steady(1.0f), loud) != 0u);
        render(device, 1024);
        CHECK(render(device, 2048).peak() == 0.0f);

        for (int field = 0; field < 3; ++field) {
            VoiceParams params = at({1.0f, 0.0f, -3.0f}, 1.0f, 20.0f);
            if (field == 0) params.minDistance = bad;
            if (field == 1) params.maxDistance = bad;
            if (field == 2) params.occlusion = bad;
            REQUIRE(device.play(tone(200.0f, 1.0f), params) != 0u);
        }
        REQUIRE(device.play(tone(250.0f, 1.0f), flat()) != 0u);
        const Block block = render(device, 4096);
        CHECK(block.finite());
        CHECK(block.level() > 0.1f);
        CHECK(block.peak() < 8.0f);
    }

    // The master gain likewise: anything but a finite non-negative number is silence.
    for (const float bad : {-0.5f, INFINITE, NOT_A_NUMBER}) {
        Device device;
        openMix(device);
        device.setMasterVolume(bad);
        CHECK(device.masterVolume() == 0.0f);
        REQUIRE(device.play(steady(1.0f), flat()) != 0u);
        const Block block = render(device, 2048);
        CHECK(block.finite());
        CHECK(block.peak() == 0.0f);
    }
    Device device;
    device.setMasterVolume(0.5f);
    CHECK(device.masterVolume() == 0.5f);
    openMix(device);
    REQUIRE(device.play(steady(1.0f), flat()) != 0u);
    render(device, 1024);
    const float half = render(device, 2048).level();
    device.setMasterVolume(1.0f);
    render(device, 1024);
    CHECK_NEAR(half / render(device, 2048).level(), 0.5f, 0.01f);
}

TEST("a pitch of zero or NaN does not stop a voice's clock") {
    for (const float pitch : {0.0f, -1.0f, NOT_A_NUMBER}) {
        Device device;
        openMix(device);
        VoiceParams params = flat();
        params.pitch = pitch;
        const auto voice = device.play(tone(200.0f, 1.0f), params);
        REQUIRE(voice != 0u);
        render(device, RATE);
        // A second at the floor of a hundredth of the speed.
        CHECK(device.voiceCursor(voice) > 0.005f);
        CHECK(device.voiceCursor(voice) < 0.02f);
        CHECK(device.isVoiceActive(voice));
    }
}

TEST("a pitch past 16, however wild, plays at 16") {
    for (const float pitch : {16.0f, 1e6f, 1e9f, INFINITE}) {
        Device device;
        openMix(device);
        VoiceParams params = flat();
        params.pitch = pitch;
        const auto voice = device.play(steady(2.0f), params);
        REQUIRE(voice != 0u);
        // A sixteenth of a second at sixteen times the speed: a second into the clip.
        render(device, RATE / 16);
        CHECK_NEAR(device.voiceCursor(voice), 1.0f, 0.02f);
        CHECK(device.isVoiceActive(voice));
    }
}

TEST("an occluded voice is quieter by occludedGain and duller: what is above its cutoff falls further") {
    namespace Acoustics = Vkm::Audio::Acoustics;
    for (const float occlusion : {0.5f, 1.0f}) {
        VoiceParams open     = flat();
        VoiceParams occluded = flat();
        occluded.occlusion = occlusion;
        const Vkm::Audio::Clip low  = tone(46.875f, 1.0f);
        const Vkm::Audio::Clip high = tone(9375.0f, 1.0f);
        const float lowKept  = levelOf(low, occluded) / levelOf(low, open);
        const float highKept = levelOf(high, occluded) / levelOf(high, open);
        CHECK_NEAR(lowKept, Acoustics::occludedGain(occlusion), 0.02f);
        // A one-pole low-pass keeps about cutoff / frequency of what is far above it.
        const float above = Acoustics::occludedCutoff(occlusion) / 9375.0f;
        CHECK(highKept < Acoustics::occludedGain(occlusion) * std::min(1.0f, 1.5f * above));
        CHECK(highKept < lowKept * 0.8f);
    }
    // No occlusion, no filter: a high tone passes as whole as a low one.
    CHECK_NEAR(levelOf(tone(9375.0f, 1.0f), flat()) / levelOf(tone(46.875f, 1.0f), flat()), 1.0f, 0.01f);
}

TEST("a voice ringing in the room rings on after it ends, at the level set, and stopAllVoices silences the tail") {
    const auto tailAfterBurst = [](float wet, bool reverb, Block* after) {
        Device device;
        openMix(device);
        device.setRoomReverb(1.0f, wet);
        VoiceParams params = flat();
        params.reverb = reverb;
        REQUIRE(device.play(tone(400.0f, 0.05f), params) != 0u);
        render(device, 4800);
        // Well past the burst: what is heard is the room.
        const Block tail = render(device, 9600);
        if (after) {
            device.stopAllVoices();
            render(device, 1024);
            *after = render(device, 4800);
        }
        return tail.level();
    };

    CHECK(tailAfterBurst(0.5f, false, nullptr) == 0.0f);
    Block       stopped;
    const float full = tailAfterBurst(0.5f, true, &stopped);
    CHECK(full > 1e-3f);
    CHECK(stopped.peak() == 0.0f);
    CHECK_NEAR(tailAfterBurst(0.25f, true, nullptr) / full, 0.5f, 0.01f);
    CHECK(tailAfterBurst(0.0f, true, nullptr) == 0.0f);

    // Its decay is the one set: after a second at a second's decay, 60 dB down from where it was.
    Device device;
    openMix(device);
    device.setRoomReverb(1.0f, 0.5f);
    VoiceParams params = flat();
    params.reverb = true;
    REQUIRE(device.play(tone(400.0f, 0.05f), params) != 0u);
    render(device, 4800);
    const float early = render(device, 4800).level();
    render(device, RATE - 4800);
    const float late   = render(device, 4800).level();
    const float fallen = 20.0f * std::log10(early / late);
    CHECK(fallen > 45.0f);
    CHECK(fallen < 75.0f);
}

TEST("stopAllVoices silences the room under a voice still sounding, however short the mixer's blocks") {
    // Shorter than a stop's ramp, and longer.
    for (const size_t block : {64u, 128u, 1024u}) {
        Device device;
        openMix(device);
        device.setRoomReverb(1.0f, 0.5f);
        VoiceParams params = flat();
        params.reverb = true;
        REQUIRE(device.play(tone(400.0f, 1.0f), params) != 0u);
        for (size_t frame = 0; frame < 4800; frame += block) render(device, block);
        device.stopAllVoices();
        // Past the ramp, with room to spare.
        for (size_t frame = 0; frame < 1024; frame += block) render(device, block);
        float after = 0.0f;
        for (size_t frame = 0; frame < 4800; frame += block) {
            after = std::max(after, render(device, block).peak());
        }
        CHECK(after == 0.0f);
    }
}

TEST("a playing voice taken out of the room stops feeding its reverb, so the tail it left dies away") {
    const auto tailAfterStop = [](bool ringing) {
        Device device;
        openMix(device);
        device.setRoomReverb(0.5f, 0.5f);
        VoiceParams params = flat();
        params.loop   = true;
        params.reverb = true;
        const auto voice = device.play(tone(400.0f, 1.0f), params);
        REQUIRE(voice != 0u);
        render(device, 4800);

        params.reverb = ringing;
        device.updateVoice(voice, params);
        // A second at half a second's decay: what it put in before is 120 dB down.
        render(device, RATE);
        device.stopVoice(voice);
        render(device, 1024);
        return render(device, 4800).level();
    };
    CHECK(tailAfterStop(true) > 1e-3f);
    CHECK(tailAfterStop(false) < 1e-5f);
}

TEST("a second open of another mixer is refused and said, or stops the program where asserts run") {
#if defined(_WIN32)
    Vkm::Test::skip("a child process here needs fork");
#else
    const auto ended = Vkm::Test::inChild([] {
        Device device;
        REQUIRE(device.openOffline(RATE, 6));
        // The same mixer again is the one open; another shape, or a device, is not.
        if (!device.openOffline(RATE, 6)) _exit(1);
        if (device.openOffline(RATE, 2)) _exit(1);
        if (device.open()) _exit(1);
    });
    if (VKM_TEST_LIBRARY_ASSERTS) {
        CHECK(ended == Vkm::Test::Ended::Stopped);
    } else {
        CHECK(ended == Vkm::Test::Ended::Returned);
        CHECK(Vkm::Test::logText().find("Another mixer is open") != std::string::npos);
        CHECK(Vkm::Test::logText().find("A mixer with no device is open") != std::string::npos);
    }
#endif
}
