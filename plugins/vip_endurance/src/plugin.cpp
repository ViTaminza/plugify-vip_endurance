// vip_endurance — Plugify port of the Pisex VIP module "VIP_Endurance" / "[VIP] Endurance"
// (ref/src/cs2-vip-modules/VIP_Endurance/source/vip_endurance.cpp, 150 lines).
//
// Behaviour kept identical:
//   * feature "Endurance" (VIP_BOOL, TOGGLABLE, with the original's toggle callback that stores
//     the new status and lets the toggle through).
//   * every game frame, for each slot flagged by the loaded/disconnect/toggle callbacks whose
//     pawn is alive: when m_flVelocityModifier < 1.0 it is set to 1.0 - i.e. endurance cancels
//     the movement slowdown of taking damage for VIPs with the feature.
//   * VIP_OnClientLoaded reads the feature, VIP_OnClientDisconnect clears the slot.
//
// Port notes:
//   * the original's IServerGameDLL::GameFrame SourceHook is the per-frame work the Plugify host
//     gives every plugin in OnPluginUpdate (ServerPostSimulate) - the same point in the frame as
//     pisexcompat's own per-frame pump.
//   * CCSPlayerController::FromSlot + m_hPlayerPawn + IsAlive -> pisex::IsAlive;
//     m_flVelocityModifier read/write -> s2sdk::GetClientSpeed/SetClientSpeed (the engine's own
//     accessors of that field, the same ones the sibling vip_speed port uses).
//   * the original's GameFrame checked all 64 slots, but only flagged ones could ever be
//     written; the port iterates only those (same result, no pawn queries for everyone else).
//   * the original's OnStartupServer (entity-system pointers) is not needed - the port keeps no
//     engine pointers.

#include <pisex/pisex.hpp>
#include <plg/plugin.hpp>
#include <plugin_export.h>
#include <s2sdk/s2sdk.hpp>
#include <vip_api.hpp>

#include "endurance_logic.hpp"

#include <chrono>
#include <cstdint>
#include <exception>
#include <string>
#include <string_view>

namespace {

// The original code's unqualified spellings (VIP_BOOL, TOGGLABLE, ENABLED, ...).
using namespace vip;

constexpr const char* kLogTag = "VIP-Endurance"; // GetLogTag() of the original
constexpr const char* kFeature = "Endurance";

IVIPApi* g_pVIPCore = &IVIPApi::Get();

void LogInfo(std::string_view message) {
	if (plg::Log) {
		plg::Log(message, plg::Severity::Info, plg::source_location::current());
	}
}

void LogError(std::string_view message) {
	if (plg::Log) {
		plg::Log(message, plg::Severity::Error, plg::source_location::current());
	}
}

// The original's ErrorLog("[%s] ...", GetLogTag()).
void LogTaggedError(const std::string& message) {
	LogError(std::string("[") + kLogTag + "] " + message);
}

class EndurancePlugin final : public plg::Plugin {
public:
	plg::PluginResult OnPluginStart() override;
	plg::PluginResult OnPluginUpdate(std::chrono::milliseconds dt) override;
	plg::PluginResult OnPluginEnd() override;

private:
	// VIPEndurance::GameFrame of the original (the per-frame SourceHook).
	void GameFrame();

	vip_endurance::State m_state;
};

EndurancePlugin g_plugin;

void EndurancePlugin::GameFrame() {
	// if(g_pVIPCore->VIP_IsVIPLoaded()) { ... }
	if (!g_pVIPCore->VIP_IsVIPLoaded()) {
		return;
	}
	for (int i = 0; i < vip_endurance::kMaxSlots; ++i) {
		if (!m_state.enabled[i]) {
			continue;
		}
		// CCSPlayerController::FromSlot + m_hPlayerPawn + IsAlive.
		if (!pisex::IsAlive(i)) {
			continue;
		}
		// if(g_bEndurance[i] && pPlayerPawn->m_flVelocityModifier() < 1.0)
		//     pPlayerPawn->m_flVelocityModifier() = 1.0;
		if (!__s2sdk_GetClientSpeed || !__s2sdk_SetClientSpeed) {
			continue;
		}
		try {
			if (s2sdk::GetClientSpeed(i) < 1.0f) {
				s2sdk::SetClientSpeed(i, 1.0f);
			}
		} catch (...) {
		}
	}
}

plg::PluginResult EndurancePlugin::OnPluginStart() {
	try {
		// Never fatal: only logs when the s2sdk bindings are missing.
		pisex::Init("vip_endurance");

		// g_pVIPCore->VIP_OnClientLoaded(OnClientLoaded); VIP_OnClientDisconnect(OnClientDisconnect);
		g_pVIPCore->VIP_OnClientLoaded([this](int iSlot, bool bIsVIP) {
			// g_bEndurance[iSlot] = bIsVIP ? VIP_GetClientFeatureBool(iSlot, "Endurance") : false
			vip_endurance::OnClientLoaded(m_state, iSlot, bIsVIP,
										  bIsVIP && g_pVIPCore->VIP_GetClientFeatureBool(iSlot, kFeature));
		});
		g_pVIPCore->VIP_OnClientDisconnect([this](int iSlot, bool /*bIsVIP*/) {
			vip_endurance::OnClientDisconnect(m_state, iSlot);
		});

		// VIP_RegisterFeature("Endurance", VIP_BOOL, TOGGLABLE, nullptr, OnTogglable):
		// g_bEndurance[iSlot] = (eNewStatus == ENABLED); return false;
		g_pVIPCore->VIP_RegisterFeature(
			kFeature, VIP_BOOL, TOGGLABLE, nullptr,
			[this](int iSlot, const char* /*szFeature*/, VIP_ToggleState /*eOldStatus*/, VIP_ToggleState& eNewStatus) {
				vip_endurance::OnToggle(m_state, iSlot, eNewStatus == ENABLED);
				return false; // the original let the toggle through
			});

		LogInfo(std::string("[") + kLogTag + "] ready");
	} catch (const std::exception& ex) {
		LogTaggedError(std::string("OnPluginStart failed: ") + ex.what());
	} catch (...) {
		LogTaggedError("OnPluginStart failed with an unknown exception");
	}
	return {}; // never Failed: that stops every plugify plugin
}

plg::PluginResult EndurancePlugin::OnPluginUpdate(std::chrono::milliseconds /*dt*/) {
	try {
		pisex::Poll();
		GameFrame(); // the original's per-frame SourceHook
	} catch (const std::exception& ex) {
		LogTaggedError(std::string("OnPluginUpdate failed: ") + ex.what());
	} catch (...) {
		LogTaggedError("OnPluginUpdate failed with an unknown exception");
	}
	return {};
}

plg::PluginResult EndurancePlugin::OnPluginEnd() {
	try {
		g_pVIPCore->VIP_OnClientLoadedUnregister();
		g_pVIPCore->VIP_OnClientDisconnectUnregister();
		// The vip API has no feature unregister: re-registering the feature without callbacks
		// replaces the core's entry with null delegates, so the core keeps no pointer into this
		// library after it is unloaded (same value/feature type: the menu entry is unchanged).
		g_pVIPCore->VIP_RegisterFeature(kFeature, VIP_BOOL, TOGGLABLE);
		pisex::Shutdown();
	} catch (...) {
		// nothing left to do at unload
	}
	return {};
}

} // namespace

PLUGIFY_PLUGIN(PLUGIN_API, &g_plugin)
