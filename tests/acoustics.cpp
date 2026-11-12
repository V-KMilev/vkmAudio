// The acoustics: the room's estimate and what stands between a sound and the ear, over casts made here.

#include <cmath>
#include <optional>
#include <set>
#include <vector>

#include <glm/glm.hpp>

#include "vkm_audio/acoustics.h"

#include "harness/check.h"

namespace {

namespace Acoustics = Vkm::Audio::Acoustics;

using Acoustics::Strike;

/**
 * @brief The inside of a box about the origin, @p half on each axis, seen from within.
 */
struct Box {
    glm::vec3 half;
    bool      roof = true;  ///< Without one, a ray going up leaves.

    bool operator()(glm::vec3 origin, glm::vec3 direction, float reach, Strike& strike) const {
        std::optional<Strike> nearest;
        for (int axis = 0; axis < 3; ++axis) {
            if (direction[axis] == 0.0f) continue;
            const float wall = direction[axis] > 0.0f ? half[axis] : -half[axis];
            if (axis == 1 && wall > 0.0f && !roof) continue;
            const float distance = (wall - origin[axis]) / direction[axis];
            if (distance < 0.0f || distance > reach || (nearest && distance >= nearest->distance)) continue;
            // A wall's plane counts only within the box: with no roof, a ray up leaves over the walls.
            const glm::vec3 point = origin + direction * distance;
            const glm::vec3 past  = glm::abs(point) - half - 1e-4f;
            if (std::max(past.x, std::max(past.y, past.z)) > 0.0f) continue;
            Strike hit;
            hit.distance     = distance;
            hit.point        = point;
            hit.normal       = glm::vec3(0.0f);
            hit.normal[axis] = direction[axis] > 0.0f ? -1.0f : 1.0f;
            nearest          = hit;
        }
        if (!nearest) return false;
        strike = *nearest;
        return true;
    }
};

/// Nothing anywhere.
bool nothing(glm::vec3, glm::vec3, float, Strike&) { return false; }

/**
 * @brief The plane z = @p z, all but a gap where |x| < @p gap; a ray stops on it as on a wall.
 */
struct Wall {
    float z;
    float gap = 0.0f;

    bool operator()(glm::vec3 origin, glm::vec3 direction, float reach, Strike& strike) const {
        if (direction.z == 0.0f) return false;
        const float distance = (z - origin.z) / direction.z;
        if (distance < 0.0f || distance > reach) return false;
        const glm::vec3 point = origin + direction * distance;
        if (std::abs(point.x) < gap) return false;
        strike.distance = distance;
        strike.point    = point;
        strike.normal   = {0.0f, 0.0f, direction.z > 0.0f ? -1.0f : 1.0f};
        return true;
    }
};

/// The window's rays from @p ear, traced through @p cast.
template <typename Cast>
std::vector<Acoustics::RoomRay> window(const Cast& cast, glm::vec3 ear) {
    std::vector<Acoustics::RoomRay> rays;
    for (size_t slot = 0; slot < Acoustics::ROOM_WINDOW; ++slot) {
        rays.push_back(Acoustics::traceRoomRay(cast, ear, Acoustics::roomDirection(slot)));
    }
    return rays;
}

} // namespace

TEST("the room's directions cover the sphere evenly: every slot its own lattice point, each of unit length") {
    std::set<int> heights;
    glm::vec3     sum(0.0f);
    float lopsided = 0.0f;
    for (size_t slot = 0; slot < Acoustics::ROOM_WINDOW; ++slot) {
        const glm::vec3 direction = Acoustics::roomDirection(slot);
        CHECK_NEAR(glm::length(direction), 1.0f, 1e-5f);
        // The lattice's heights: one at each of 1 - (2i + 1) / 64.
        heights.insert(static_cast<int>(std::lround((1.0f - direction.y) * 32.0f - 0.5f)));
        sum += direction;
        // Any sixteen slots in a row, a window a quarter refilled, lean little.
        glm::vec3 run(0.0f);
        for (size_t i = 0; i < 16; ++i) run += Acoustics::roomDirection((slot + i) % Acoustics::ROOM_WINDOW);
        lopsided = std::max(lopsided, glm::length(run) / 16.0f);
    }
    CHECK(heights.size() == Acoustics::ROOM_WINDOW);
    CHECK(*heights.begin() == 0);
    CHECK(*heights.rbegin() == static_cast<int>(Acoustics::ROOM_WINDOW) - 1);
    CHECK(glm::length(sum) / static_cast<float>(Acoustics::ROOM_WINDOW) < 0.02f);
    CHECK(lopsided < 0.6f);
}

TEST("a room ray in a closed box strikes 1 + ROOM_BOUNCES times, its length the box's, and one into nothing escapes at once") {
    const Box box{{2.0f, 1.5f, 3.0f}};
    for (size_t slot = 0; slot < Acoustics::ROOM_WINDOW; ++slot) {
        const glm::vec3 ear = glm::vec3(0.2f, -0.1f, 0.4f);
        const Acoustics::RoomRay ray = Acoustics::traceRoomRay(box, ear, Acoustics::roomDirection(slot));
        CHECK(ray.strikes == 1 + Acoustics::ROOM_BOUNCES);
        CHECK(ray.escape == glm::vec3(0.0f));
        float walked = 0.0f;
        for (int leg = 0; leg < ray.strikes; ++leg) {
            walked += glm::length(ray.path[leg + 1] - ray.path[leg]);
            // Every strike on a wall of the box.
            const glm::vec3 point = glm::abs(ray.path[leg + 1]) - box.half;
            CHECK(std::max(point.x, std::max(point.y, point.z)) > -1e-3f);
        }
        CHECK_NEAR(ray.length, walked, 5e-3f);
    }

    const Acoustics::RoomRay open = Acoustics::traceRoomRay(nothing, glm::vec3(0.0f), {0.0f, 0.0f, -1.0f});
    CHECK(open.strikes == 0);
    CHECK(open.length == 0.0f);
    CHECK(open.escape == glm::vec3(0.0f, 0.0f, -1.0f));

    // No roof: off the floor, a ray goes out through the top.
    const Box                pit{{2.0f, 1.5f, 3.0f}, false};
    const glm::vec3 down = glm::normalize(glm::vec3(0.1f, -1.0f, 0.0f));
    const Acoustics::RoomRay out = Acoustics::traceRoomRay(pit, glm::vec3(0.0f), down);
    CHECK(out.strikes == 1);
    CHECK_NEAR(out.escape, glm::reflect(down, glm::vec3(0.0f, 1.0f, 0.0f)), 1e-6f);
}

TEST("open ground is no enclosure and no reverb, and a shut box is all enclosure with its size") {
    CHECK(Acoustics::estimateRoom(nullptr, 0).enclosure == 0.0f);
    const std::vector<Acoustics::RoomRay> open = window(nothing, glm::vec3(0.0f));
    const Acoustics::Room none = Acoustics::estimateRoom(open.data(), open.size());
    CHECK(none.enclosure == 0.0f);
    CHECK(none.meanFreePath == 0.0f);
    CHECK(Acoustics::reverbFor(none, 1.0f).wet == 0.0f);

    // Only a floor: some rays strike once, and that is still out in the open.
    const Box                             yard{{1000.0f, 1.7f, 1000.0f}, false};
    const std::vector<Acoustics::RoomRay> floor = window(yard, glm::vec3(0.0f));
    const Acoustics::Room ground = Acoustics::estimateRoom(floor.data(), floor.size());
    CHECK(ground.enclosure > 0.1f);
    CHECK(ground.enclosure < 0.25f);
    CHECK(Acoustics::reverbFor(ground, 1.0f).wet == 0.0f);

    // A box 4 by 3 by 6: every leg strikes, each as long as the box's mean free path, 4V/S, near enough.
    const Box                             box{{2.0f, 1.5f, 3.0f}};
    const std::vector<Acoustics::RoomRay> inside = window(box, glm::vec3(0.0f));
    const Acoustics::Room shut = Acoustics::estimateRoom(inside.data(), inside.size());
    CHECK(shut.enclosure == 1.0f);
    CHECK_NEAR(shut.meanFreePath, 4.0f * 72.0f / 108.0f, 0.6f);

    // Half the window counts as half.
    CHECK(Acoustics::estimateRoom(inside.data(), 32).enclosure == 1.0f);
    std::vector<Acoustics::RoomRay> mixed = inside;
    for (size_t i = 0; i < mixed.size(); i += 2) mixed[i] = open[i];
    CHECK_NEAR(Acoustics::estimateRoom(mixed.data(), mixed.size()).enclosure, 0.5f, 1e-6f);
}

TEST("a room's reverb follows Eyring's decay held to its range, and the strength scales its level") {
    // 0.04 l / -ln(1 - a), with a lived-in room's absorption of a fifth.
    const auto eyring = [](float meanFreePath) { return 0.04f * meanFreePath / -std::log(0.8f); };

    Acoustics::Room room;
    room.enclosure    = 1.0f;
    room.meanFreePath = 10.0f;
    const Acoustics::Reverb shut = Acoustics::reverbFor(room, 1.0f);
    CHECK_NEAR(shut.decaySeconds, eyring(10.0f), 1e-4f);
    CHECK(shut.wet > 0.0f);
    CHECK_NEAR(Acoustics::reverbFor(room, 0.5f).wet, shut.wet * 0.5f, 1e-6f);
    CHECK(Acoustics::reverbFor(room, 0.5f).decaySeconds == shut.decaySeconds);
    CHECK(Acoustics::reverbFor(room, 0.0f).wet == 0.0f);

    room.meanFreePath = 200.0f;
    CHECK(Acoustics::reverbFor(room, 1.0f).decaySeconds == 6.0f);
    room.meanFreePath = 0.5f;
    CHECK(Acoustics::reverbFor(room, 1.0f).decaySeconds == 0.2f);

    // A leaky room decays sooner and is quieter.
    room.meanFreePath = 10.0f;
    room.enclosure    = 0.6f;
    const Acoustics::Reverb leaky = Acoustics::reverbFor(room, 1.0f);
    CHECK(leaky.decaySeconds < shut.decaySeconds);
    CHECK(leaky.wet < shut.wet);
    CHECK(leaky.wet > 0.0f);
}

TEST("the occlusion rays aim at a source's middle, then either side of it and above and below, across the line of sight") {
    const glm::vec3 ear(1.0f, 2.0f, 3.0f);
    const glm::vec3 source(4.0f, 2.5f, -7.0f);
    const auto targets = Acoustics::occlusionTargets(ear, source, 0.8f);
    CHECK(targets[0] == source);
    const glm::vec3 line = glm::normalize(source - ear);
    for (size_t i = 1; i < Acoustics::OCCLUSION_RAYS; ++i) {
        CHECK_NEAR(glm::length(targets[i] - source), 0.8f, 1e-5f);
        CHECK_NEAR(glm::dot(targets[i] - source, line), 0.0f, 1e-5f);
    }
    // In pairs either way of the source, the pairs at right angles: the first level, the second up and down.
    CHECK_NEAR(targets[1] + targets[2], 2.0f * source, 1e-5f);
    CHECK_NEAR(targets[3] + targets[4], 2.0f * source, 1e-5f);
    CHECK_NEAR(glm::dot(targets[1] - source, targets[3] - source), 0.0f, 1e-5f);
    CHECK_NEAR(targets[1].y, source.y, 1e-5f);
    CHECK(targets[3].y > source.y + 0.7f);

    // At the ear, every one is the source.
    for (const glm::vec3& target : Acoustics::occlusionTargets(source, source, 0.8f)) CHECK(target == source);
}

TEST("an occlusion ray stops short of its target, a target too near is not cast, and one in a wall is pulled halfway back") {
    // A floor at y = 0 and a sound half a meter over it: the target below is in the floor.
    const auto floor = [](glm::vec3 origin, glm::vec3 direction, float reach, Strike& strike) {
        if (direction.y >= 0.0f) return false;
        const float distance = -origin.y / direction.y;
        if (distance > reach) return false;
        strike.distance = distance;
        strike.point    = origin + direction * distance;
        strike.normal   = {0.0f, 1.0f, 0.0f};
        return true;
    };
    const glm::vec3 ear(0.0f, 1.5f, 10.0f);
    const glm::vec3 source(0.0f, 0.5f, 0.0f);

    std::vector<glm::vec3> targets;
    std::vector<float>     reaches;
    int blocked = 0;
    const auto reach = [&](glm::vec3 origin, glm::vec3 direction, float most, Strike& strike) {
        if (origin == ear) reaches.push_back(most);
        return floor(origin, direction, most, strike);
    };
    const auto visit = [&](const glm::vec3& target, const Strike* stopped) {
        targets.push_back(target);
        blocked += stopped ? 1 : 0;
    };
    Acoustics::castOcclusionRays(reach, ear, source, 1.0f, visit);
    REQUIRE(targets.size() == Acoustics::OCCLUSION_RAYS);
    for (size_t i = 0; i < targets.size(); ++i) {
        CHECK_NEAR(reaches[i], glm::length(targets[i] - ear) - Acoustics::SOURCE_CLEARANCE, 1e-5f);
        CHECK(targets[i].y > 0.0f);
    }
    // The one below went halfway to the floor: a quarter meter down, and nothing stops the ear's ray to it.
    float lowest = 1.0f;
    for (const glm::vec3& target : targets) lowest = std::min(lowest, target.y);
    CHECK_NEAR(lowest, 0.25f, 1e-4f);
    CHECK(blocked == 0);

    // An ear at the source casts nothing.
    int cast = 0;
    Acoustics::castOcclusionRays(
        floor,
        source + glm::vec3(0.0f, 0.0f, 0.05f),
        source,
        0.02f,
        [&](const glm::vec3&, const Strike*) { ++cast; }
    );
    CHECK(cast == 0);
}

TEST("the share of rays stopped is 1 behind a wall, part through a doorway and 0 in the open") {
    const glm::vec3 ear(0.0f);
    const glm::vec3 source(0.0f, 0.0f, -10.0f);
    CHECK(Acoustics::blockedShare(nothing, ear, source, 1.0f) == 0.0f);
    CHECK(Acoustics::blockedShare(Wall{-5.0f}, ear, source, 1.0f) == 1.0f);
    // A doorway as wide as the middle, up and down rays: the two side ones are stopped.
    CHECK_NEAR(Acoustics::blockedShare(Wall{-5.0f, 0.3f}, ear, source, 1.0f), 0.4f, 1e-6f);
}

TEST("occlusion keeps from all the gain to OCCLUDED_GAIN, its cutoff falls on a log scale, and approach is framerate-free") {
    CHECK(Acoustics::occludedGain(0.0f) == 1.0f);
    CHECK_NEAR(Acoustics::occludedGain(1.0f), Acoustics::OCCLUDED_GAIN, 1e-6f);
    CHECK_NEAR(Acoustics::occludedGain(0.5f), 0.5f * (1.0f + Acoustics::OCCLUDED_GAIN), 1e-6f);
    CHECK(Acoustics::occludedGain(-1.0f) == 1.0f);
    CHECK_NEAR(Acoustics::occludedGain(3.0f), Acoustics::OCCLUDED_GAIN, 1e-6f);

    CHECK_NEAR(Acoustics::occludedCutoff(0.0f), Acoustics::OPEN_CUTOFF, 1e-2f);
    CHECK_NEAR(Acoustics::occludedCutoff(1.0f), Acoustics::OCCLUDED_CUTOFF, 1e-2f);
    const float middle = std::sqrt(Acoustics::OPEN_CUTOFF * Acoustics::OCCLUDED_CUTOFF);
    CHECK_NEAR(Acoustics::occludedCutoff(0.5f), middle, 0.5f);
    CHECK_NEAR(
        Acoustics::occludedCutoff(0.25f) / Acoustics::occludedCutoff(0.5f),
        Acoustics::occludedCutoff(0.75f) / Acoustics::occludedCutoff(1.0f),
        1e-4f
    );

    const float seconds = Acoustics::OCCLUSION_SECONDS;
    const float whole   = Acoustics::approach(0.0f, 1.0f, 0.1f, seconds);
    const float half    = Acoustics::approach(0.0f, 1.0f, 0.05f, seconds);
    const float halves  = Acoustics::approach(half, 1.0f, 0.05f, seconds);
    CHECK_NEAR(whole, halves, 1e-6f);
    CHECK(whole > 0.5f);
    CHECK(whole < 1.0f);
    // Near enough, it lands on its target exactly.
    CHECK(Acoustics::approach(0.9995f, 1.0f, 0.001f, seconds) == 1.0f);
    CHECK(Acoustics::approach(0.3f, 1.0f, 0.0f, 1.0f) == 0.3f);
    CHECK(Acoustics::approach(0.3f, 1.0f, 0.1f, 0.0f) == 1.0f);
}
