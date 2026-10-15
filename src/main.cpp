// hyprseam — turn partially shared monitor edges from dead ends into seams.
//
// Hyprland clamps the pointer to the monitor layout: pushing against a spot
// where the current monitor's edge has no neighbor simply swallows the
// motion. hyprseam detects that swallow and warps the pointer to the nearest
// point on the monitor on the other side of the pushed edge, so the push
// continues on the neighbor instead of hitting a hard wall.

#define WLR_USE_UNSTABLE

#include <hyprland/src/plugins/PluginAPI.hpp>

#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/pointer/PointerManager.hpp>
#include <hyprland/src/state/MonitorState.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

inline HANDLE PHANDLE = nullptr;

// hyprland/src/protocols/InputCapture.hpp cannot be included here: it pulls
// in libeis headers, which are not part of hyprland.pc's build interface (and
// libei is not installed everywhere Hyprland is). Declare the tiny public
// surface this plugin uses instead. Both symbols are exported by the
// compositor, and the PLUGIN_INIT version guard pins the exact build.
class CInputCaptureProtocol {
  public:
    bool isCaptured();
};

namespace NProtocols {
    extern std::unique_ptr<CInputCaptureProtocol> inputCapture; // NOLINT
} // namespace NProtocols

namespace {

// Pointer motion below this many logical pixels is treated as blocked.
constexpr double BLOCKED_EPSILON = 0.02;
// The pointer must sit within this many logical pixels of the pushed edge for
// that edge to be considered the one it is stuck on.
constexpr double EDGE_TRIGGER_EPSILON = 0.5;
// Two edges count as shared when they align within this many logical pixels.
constexpr double EDGE_SHARE_EPSILON = 0.5;
// How far inside the neighbor the pointer is placed on transfer.
constexpr double TRANSFER_INSET = 1.0;

using tMove = void (*)(Pointer::CPointerManager*, const Vector2D&);

CFunctionHook* g_pMoveHook     = nullptr;
tMove          g_pOriginalMove = nullptr;

// Monitors the pointer may use — the same set Hyprland clamps the layout to.
bool usable(const PHLMONITOR& mon) {
    return mon && mon->m_enabled && mon->m_output && !mon->isMirror();
}

// The monitor the position belongs to, or the closest one.
PHLMONITOR monitorOf(const Vector2D& pos) {
    PHLMONITOR best;
    double     bestDistance = std::numeric_limits<double>::max();

    for (const auto& mon : State::monitorState()->monitors()) {
        if (!usable(mon))
            continue;

        const double DISTANCE = mon->logicalBox().closestPoint(pos).distanceSq(pos);
        if (DISTANCE < bestDistance) {
            best         = mon;
            bestDistance = DISTANCE;
        }
    }

    return best;
}

// Nearest transfer point if the pointer is pinned against the edge of
// `current` in the given axis direction and a monitor partially shares that
// edge. Returns nullopt when the edge is not the one the pointer is stuck on,
// or no neighbor sits behind it.
std::optional<Vector2D> scanAxis(const Vector2D& pos, const Vector2D& desired, const PHLMONITOR& current, const int axis, const int direction) {
    const CBox CURRENT_BOX = current->logicalBox();

    const double EDGE      = axis == 0 ? (direction > 0 ? CURRENT_BOX.x + CURRENT_BOX.w : CURRENT_BOX.x)
                                       : (direction > 0 ? CURRENT_BOX.y + CURRENT_BOX.h : CURRENT_BOX.y);
    const double POS_COORD = axis == 0 ? pos.x : pos.y;
    if (std::abs(POS_COORD - EDGE) > EDGE_TRIGGER_EPSILON)
        return std::nullopt;

    std::optional<Vector2D> best;
    double                  bestDistance = std::numeric_limits<double>::max();

    for (const auto& mon : State::monitorState()->monitors()) {
        if (!usable(mon) || mon == current)
            continue;

        const CBox BOX = mon->logicalBox();

        // The neighbor must sit exactly on the far side of the pushed edge.
        const double SHARED = axis == 0 ? (direction > 0 ? BOX.x : BOX.x + BOX.w)
                                        : (direction > 0 ? BOX.y : BOX.y + BOX.h);
        if (std::abs(SHARED - EDGE) > EDGE_SHARE_EPSILON)
            continue;

        // …and the two monitors must overlap on the perpendicular axis.
        const double PERP_LOW_A  = axis == 0 ? CURRENT_BOX.y : CURRENT_BOX.x;
        const double PERP_HIGH_A = axis == 0 ? CURRENT_BOX.y + CURRENT_BOX.h : CURRENT_BOX.x + CURRENT_BOX.w;
        const double PERP_LOW_B  = axis == 0 ? BOX.y : BOX.x;
        const double PERP_HIGH_B = axis == 0 ? BOX.y + BOX.h : BOX.x + BOX.w;
        if (std::min(PERP_HIGH_A, PERP_HIGH_B) - std::max(PERP_LOW_A, PERP_LOW_B) <= 0.0)
            continue;

        // Land just inside the neighbor's edge, clamped into its extent.
        Vector2D target;
        if (axis == 0) {
            target.x = direction > 0 ? BOX.x + TRANSFER_INSET : BOX.x + BOX.w - TRANSFER_INSET;
            target.y = std::clamp(pos.y, BOX.y + TRANSFER_INSET, BOX.y + BOX.h - TRANSFER_INSET);
        } else {
            target.y = direction > 0 ? BOX.y + TRANSFER_INSET : BOX.y + BOX.h - TRANSFER_INSET;
            target.x = std::clamp(pos.x, BOX.x + TRANSFER_INSET, BOX.x + BOX.w - TRANSFER_INSET);
        }

        const double DISTANCE = target.distanceSq(desired);
        if (DISTANCE < bestDistance) {
            best         = target;
            bestDistance = DISTANCE;
        }
    }

    return best;
}

// Where to put the pointer when a push from pos towards desired was blocked,
// or nullopt when the block is not a dead end of the driven edge.
std::optional<Vector2D> findTransferTarget(const Vector2D& pos, const Vector2D& desired) {
    const auto CURRENT = monitorOf(pos);
    if (!CURRENT)
        return std::nullopt;

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
        CONSIDER(scanAxis(pos, desired, CURRENT, 0, 1));
    if (PUSH.x < -1e-6)
        CONSIDER(scanAxis(pos, desired, CURRENT, 0, -1));
    if (PUSH.y > 1e-6)
        CONSIDER(scanAxis(pos, desired, CURRENT, 1, 1));
    if (PUSH.y < -1e-6)
        CONSIDER(scanAxis(pos, desired, CURRENT, 1, -1));

    return best;
}

void hkMove(Pointer::CPointerManager* self, const Vector2D& deltaLogical) {
    if (std::isnan(deltaLogical.x) || std::isnan(deltaLogical.y)) {
        g_pOriginalMove(self, deltaLogical);
        return;
    }

    const Vector2D POS_BEFORE = self->position();

    g_pOriginalMove(self, deltaLogical);

    const Vector2D POS_AFTER = self->position();

    // The move went through — nothing to do.
    if (POS_AFTER.distanceSq(POS_BEFORE) > BLOCKED_EPSILON * BLOCKED_EPSILON)
        return;

    // A client is steering the pointer: a pointer constraint (locked or
    // confined, e.g. games) or an input capture session. Those are not
    // layout dead ends; leave them alone.
    if (g_pInputManager->isConstrained())
        return;
    if (NProtocols::inputCapture && NProtocols::inputCapture->isCaptured())
        return;

    const auto TARGET = findTransferTarget(POS_AFTER, POS_BEFORE + deltaLogical);
    if (!TARGET)
        return;

    self->warpTo(*TARGET);
}

} // namespace

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    // Refuse to run against a mismatched Hyprland: this plugin relies on
    // exact struct layouts and exported symbols of one specific build.
    if (std::string_view{__hyprland_api_get_hash()} != std::string_view{__hyprland_api_get_client_hash()}) {
        HyprlandAPI::addNotification(PHANDLE, "[hyprseam] Hyprland ABI mismatch — run `hyprpm update` to rebuild", CHyprColor{1.0, 0.2, 0.2, 1.0}, 10000);
        throw std::runtime_error("hyprseam: Hyprland was updated, rebuild needed (hyprpm update)");
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpmf-conversions"
    g_pMoveHook = HyprlandAPI::createFunctionHook(PHANDLE, (void*)&Pointer::CPointerManager::move, (void*)&hkMove);
    if (g_pMoveHook && g_pMoveHook->hook())
        g_pOriginalMove = (tMove)g_pMoveHook->m_original;
#pragma GCC diagnostic pop

    if (!g_pOriginalMove) {
        HyprlandAPI::addNotification(PHANDLE, "[hyprseam] failed to hook CPointerManager::move", CHyprColor{1.0, 0.2, 0.2, 1.0}, 10000);
        throw std::runtime_error("hyprseam: failed to hook CPointerManager::move");
    }

    return {"hyprseam", "Seamless pointer transfer across partially shared monitor edges", "vanyan", "0.1.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    // Function hooks are owned by the plugin system and removed on unload.
}
