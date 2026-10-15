// Synthetic-layout tests for the pure dead-end transfer geometry.
//
//   cmake -S . -B build-tests -DHYPRSEAM_BUILD_TESTS=ON
//   cmake --build build-tests && ctest --test-dir build-tests
//
// Every "layout" here is just a set of boxes: no Hyprland, no session.
// All coordinates are written as doubles: Vector2D has both (int, int) and
// (double, double) constructors, so brace lists of integer literals are
// ambiguous during list-initialization.

#include "../src/geometry.hpp"

#include <cmath>
#include <cstdio>

using namespace hyprseam;

static int g_failures = 0;

static void expectTarget(const char* name, const std::optional<Vector2D>& got, const std::optional<Vector2D>& want) {
    const auto SAME = [](const Vector2D& a, const Vector2D& b) { return std::abs(a.x - b.x) < 1e-9 && std::abs(a.y - b.y) < 1e-9; };

    if (got.has_value() == want.has_value() && (!got || SAME(*got, *want))) {
        std::printf("PASS  %s\n", name);
        return;
    }

    ++g_failures;
    std::printf("FAIL  %s\n", name);
    if (want)
        std::printf("      want (%.1f, %.1f)\n", want->x, want->y);
    else
        std::printf("      want (none)\n");
    if (got)
        std::printf("      got  (%.1f, %.1f)\n", got->x, got->y);
    else
        std::printf("      got  (none)\n");
}

int main() {
    // The motivating layout: 3440x1440 DP-1 left, 1920x1080 HDMI-A-1 right,
    // top-aligned. The strip y in [1080, 1440) of the shared edge is a dead
    // end (a "hard wall" without the plugin).
    const CBox DP1{{0.0, 0.0}, {3440.0, 1440.0}};
    const CBox HDMI{{3440.0, 0.0}, {1920.0, 1080.0}};

    expectTarget("dead strip below HDMI lands on HDMI's bottom edge", //
                 findTransferTarget({3440.0, 1200.0}, {3442.0, 1200.0}, DP1, {HDMI}, EDGE_TRIGGER_EPSILON, TRANSFER_INSET), Vector2D{3441.0, 1079.0});

    expectTarget("bottom corner of the dead strip clamps to the same point", //
                 findTransferTarget({3440.0, 1439.0}, {3441.5, 1439.0}, DP1, {HDMI}, EDGE_TRIGGER_EPSILON, TRANSFER_INSET), Vector2D{3441.0, 1079.0});

    expectTarget("push in the middle of the monitor: no transfer", //
                 findTransferTarget({1500.0, 1200.0}, {1502.0, 1200.0}, DP1, {HDMI}, EDGE_TRIGGER_EPSILON, TRANSFER_INSET), std::nullopt);

    expectTarget("push parallel to the shared edge: no transfer", //
                 findTransferTarget({3440.0, 1200.0}, {3440.0, 1198.0}, DP1, {HDMI}, EDGE_TRIGGER_EPSILON, TRANSFER_INSET), std::nullopt);

    // A tall monitor with two stacked neighbors on its right edge.
    const CBox TALL{{0.0, 0.0}, {1000.0, 2000.0}};
    const CBox TOP{{1000.0, 0.0}, {800.0, 900.0}};
    const CBox BOTTOM{{1000.0, 1000.0}, {800.0, 1000.0}};

    expectTarget("two stacked neighbors: closer upper neighbor wins", //
                 findTransferTarget({1000.0, 940.0}, {1002.0, 940.0}, TALL, {TOP, BOTTOM}, EDGE_TRIGGER_EPSILON, TRANSFER_INSET), Vector2D{1001.0, 899.0});

    expectTarget("two stacked neighbors: closer lower neighbor wins", //
                 findTransferTarget({1000.0, 960.0}, {1002.0, 960.0}, TALL, {TOP, BOTTOM}, EDGE_TRIGGER_EPSILON, TRANSFER_INSET), Vector2D{1001.0, 1001.0});

    // cursor:hotspot_padding = 10: the pointer parks 10 px inside the edge,
    // so the caller widens trigger and inset by the padding (0.5+10, 1+10).
    expectTarget("padded cursor still triggers, landing inset grows", //
                 findTransferTarget({990.0, 940.0}, {992.0, 940.0}, TALL, {TOP, BOTTOM}, EDGE_TRIGGER_EPSILON + 10.0, TRANSFER_INSET + 10.0), Vector2D{1011.0, 889.0});

    expectTarget("padded cursor beyond the tolerance: no transfer", //
                 findTransferTarget({989.0, 940.0}, {991.0, 940.0}, TALL, {TOP, BOTTOM}, EDGE_TRIGGER_EPSILON + 10.0, TRANSFER_INSET + 10.0), std::nullopt);

    // Negative coordinates: a shorter monitor to the left of the origin.
    const CBox LEFT{{-800.0, 0.0}, {800.0, 900.0}};

    expectTarget("negative coordinates transfer left", //
                 findTransferTarget({0.0, 1200.0}, {-2.0, 1200.0}, TALL, {LEFT}, EDGE_TRIGGER_EPSILON, TRANSFER_INSET), Vector2D{-1.0, 899.0});

    // Layout defects that must stay hard walls.
    const CBox GAPPED{{1001.5, 0.0}, {800.0, 2000.0}};

    expectTarget("a gap wider than half a pixel: no transfer", //
                 findTransferTarget({1000.0, 1200.0}, {1002.0, 1200.0}, TALL, {GAPPED}, EDGE_TRIGGER_EPSILON, TRANSFER_INSET), std::nullopt);

    const CBox SQUARE{{0.0, 0.0}, {1000.0, 1000.0}};
    const CBox CORNER{{1000.0, 1000.0}, {800.0, 800.0}};

    expectTarget("corner-only touch: no transfer", //
                 findTransferTarget({1000.0, 1000.0}, {1002.0, 1000.0}, SQUARE, {CORNER}, EDGE_TRIGGER_EPSILON, TRANSFER_INSET), std::nullopt);

    expectTarget("no neighbors at all: no transfer", //
                 findTransferTarget({1000.0, 1200.0}, {1002.0, 1200.0}, TALL, {}, EDGE_TRIGGER_EPSILON, TRANSFER_INSET), std::nullopt);

    // Diagonal push at a corner where both a right and a below neighbor
    // exist: the closer axis wins.
    const CBox RIGHT{{1000.0, 0.0}, {800.0, 500.0}};
    const CBox BELOW{{0.0, 2000.0}, {1000.0, 800.0}};

    expectTarget("diagonal push at a corner picks the closer neighbor", //
                 findTransferTarget({1000.0, 2000.0}, {1002.0, 2002.0}, TALL, {RIGHT, BELOW}, EDGE_TRIGGER_EPSILON, TRANSFER_INSET), Vector2D{999.0, 2001.0});

    if (g_failures > 0) {
        std::printf("\n%d test(s) FAILED\n", g_failures);
        return 1;
    }

    std::printf("\nall tests passed\n");
    return 0;
}
