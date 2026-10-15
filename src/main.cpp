// hyprseam — turn partially shared monitor edges from dead ends into seams.
//
// Hyprland clamps the pointer to the monitor layout: pushing against a spot
// where the current monitor's edge has no neighbor simply swallows the
// motion. hyprseam detects that swallow and warps the pointer to the nearest
// point on the monitor on the other side of the pushed edge, so the push
// continues on the neighbor instead of hitting a hard wall.
//
// The geometry lives in geometry.hpp (Hyprland-free, unit-tested); this file
// hooks it into the compositor.

#define WLR_USE_UNSTABLE

#include "geometry.hpp"

#include <hyprland/src/plugins/PluginAPI.hpp>

#include <hyprland/src/config/ConfigValue.hpp>
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
#include <vector>

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

// cursor:hotspot_padding (0 by default, Hyprland clamps it to 0..100). When
// set, Hyprland parks the pointer that far inside the edge, so the transfer
// trigger and the landing inset must grow by it too.
double cursorPadding() {
    static auto PADDING = CConfigValue<Config::INTEGER>("cursor:hotspot_padding");
    return std::clamp(static_cast<double>(*PADDING), 0.0, 100.0);
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

    const auto CURRENT = monitorOf(POS_AFTER);
    if (!CURRENT)
        return;

    std::vector<CBox> others;
    for (const auto& mon : State::monitorState()->monitors()) {
        if (!usable(mon) || mon == CURRENT)
            continue;
        others.push_back(mon->logicalBox());
    }

    const double PADDING = cursorPadding();

    const auto TARGET = hyprseam::findTransferTarget(POS_AFTER, POS_BEFORE + deltaLogical, CURRENT->logicalBox(), others, hyprseam::EDGE_TRIGGER_EPSILON + PADDING,
                                                     hyprseam::TRANSFER_INSET + PADDING);
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

    return {"hyprseam", "Seamless pointer transfer across partially shared monitor edges", "VanillinT", "0.1.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    // Function hooks are owned by the plugin system and removed on unload.
}
