// Pure geometry behind the dead-end transfer.
//
// Nothing in here touches Hyprland state: it takes the current monitor's box
// plus the boxes of the other monitors, and returns where the pointer should
// land. Keeping it state-free makes the tricky part unit-testable on
// synthetic layouts (see tests/geometry_test.cpp).

#pragma once

#include <hyprutils/math/Box.hpp>
#include <hyprutils/math/Vector2D.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

namespace hyprseam {

using Hyprutils::Math::CBox;
using Hyprutils::Math::Vector2D;

// The pointer must sit within this many logical pixels of the pushed edge for
// that edge to be considered the one it is stuck on. Callers widen this by
// cursor:hotspot_padding when that option is set.
inline constexpr double EDGE_TRIGGER_EPSILON = 0.5;
// Two monitor edges count as shared when they align within this many logical
// pixels.
inline constexpr double EDGE_SHARE_EPSILON = 0.5;
// How far inside the neighbor the pointer is placed on transfer (again
// widened by cursor:hotspot_padding by the caller).
inline constexpr double TRANSFER_INSET = 1.0;

// Nearest transfer point if the pointer is pinned against the edge of
// `currentBox` in the given axis direction (0 = x, 1 = y) and one of `boxes`
// partially shares that edge. Returns nullopt when the pointer is not stuck
// on that edge, or no neighbor sits behind it.
inline std::optional<Vector2D> scanAxis(const Vector2D& pos, const Vector2D& desired, const CBox& currentBox, const int axis, const int direction, const std::vector<CBox>& boxes,
                                        const double edgeTriggerEpsilon, const double inset) {
    const double EDGE      = axis == 0 ? (direction > 0 ? currentBox.x + currentBox.w : currentBox.x)
                                       : (direction > 0 ? currentBox.y + currentBox.h : currentBox.y);
    const double POS_COORD = axis == 0 ? pos.x : pos.y;
    if (std::abs(POS_COORD - EDGE) > edgeTriggerEpsilon)
        return std::nullopt;

    std::optional<Vector2D> best;
    double                  bestDistance = std::numeric_limits<double>::max();

    for (const auto& box : boxes) {
        // The neighbor must sit exactly on the far side of the pushed edge.
        const double SHARED = axis == 0 ? (direction > 0 ? box.x : box.x + box.w)
                                        : (direction > 0 ? box.y : box.y + box.h);
        if (std::abs(SHARED - EDGE) > EDGE_SHARE_EPSILON)
            continue;

        // …and the two monitors must overlap on the perpendicular axis.
        const double PERP_LOW_A  = axis == 0 ? currentBox.y : currentBox.x;
        const double PERP_HIGH_A = axis == 0 ? currentBox.y + currentBox.h : currentBox.x + currentBox.w;
        const double PERP_LOW_B  = axis == 0 ? box.y : box.x;
        const double PERP_HIGH_B = axis == 0 ? box.y + box.h : box.x + box.w;
        if (std::min(PERP_HIGH_A, PERP_HIGH_B) - std::max(PERP_LOW_A, PERP_LOW_B) <= 0.0)
            continue;

        // Land just inside the neighbor's edge, clamped into its extent.
        Vector2D target;
        if (axis == 0) {
            target.x = direction > 0 ? box.x + inset : box.x + box.w - inset;
            target.y = std::clamp(pos.y, box.y + inset, std::max(box.y + inset, box.y + box.h - inset));
        } else {
            target.y = direction > 0 ? box.y + inset : box.y + box.h - inset;
            target.x = std::clamp(pos.x, box.x + inset, std::max(box.x + inset, box.x + box.w - inset));
        }

        const double DISTANCE = target.distanceSq(desired);
        if (DISTANCE < bestDistance) {
            best         = target;
            bestDistance = DISTANCE;
        }
    }

    return best;
}

// Where to put the pointer when a push from `pos` towards `desired` was
// blocked, or nullopt when the block is not a dead end of the driven edge.
// Only the axes the push points towards are scanned; the candidate closest to
// the desired position wins.
inline std::optional<Vector2D> findTransferTarget(const Vector2D& pos, const Vector2D& desired, const CBox& currentBox, const std::vector<CBox>& boxes, const double edgeTriggerEpsilon,
                                                  const double inset) {
    const Vector2D PUSH = desired - pos;

    std::optional<Vector2D> best;
    double                  bestDistance = std::numeric_limits<double>::max();

    const auto CONSIDER = [&](std::optional<Vector2D> candidate) {
        if (!candidate)
            return;
        const double DISTANCE = candidate->distanceSq(desired);
        if (DISTANCE < bestDistance) {
            best         = candidate;
            bestDistance = DISTANCE;
        }
    };

    if (PUSH.x > 1e-6)
        CONSIDER(scanAxis(pos, desired, currentBox, 0, 1, boxes, edgeTriggerEpsilon, inset));
    if (PUSH.x < -1e-6)
        CONSIDER(scanAxis(pos, desired, currentBox, 0, -1, boxes, edgeTriggerEpsilon, inset));
    if (PUSH.y > 1e-6)
        CONSIDER(scanAxis(pos, desired, currentBox, 1, 1, boxes, edgeTriggerEpsilon, inset));
    if (PUSH.y < -1e-6)
        CONSIDER(scanAxis(pos, desired, currentBox, 1, -1, boxes, edgeTriggerEpsilon, inset));

    return best;
}

} // namespace hyprseam
