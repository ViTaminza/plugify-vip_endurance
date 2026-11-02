// vip_endurance — Plugify port of the Pisex VIP module "VIP_Endurance" / "[VIP] Endurance"
// (the original's vip_endurance.cpp, 150 lines).
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
//   * Pure s2sdk: there is no pisexcompat. The safety rules this module needs live in this file
//     (handoff/weaponskins/PITFALLS.md B1/B2, C2):
//       map gate       OnMapStart/OnMapEnd listeners plus the first OnPluginUpdate latch: not one
//                      client call before the first map or in the window between OnMapEnd and the
//                      next OnMapStart (s2sdk's client list is torn down there);
//       IsAlive(slot)  slot range, connected, in game, not SourceTV, controller present and the
//                      client alive - the original's CCSPlayerController::FromSlot +
//                      m_hPlayerPawn + IsAlive chain;
//       bindings       every __s2sdk_* export pointer is checked before it is called.
//   * the original's IServerGameDLL::GameFrame SourceHook is the per-frame work the Plugify host
//     gives every plugin in OnPluginUpdate (ServerPostSimulate) - the same point in the frame the
//     old port used for it.
//   * m_flVelocityModifier read/write -> s2sdk::GetClientSpeed/SetClientSpeed (the engine's own
//     accessors of that field, the same ones the sibling vip_speed port uses).
//   * the original's GameFrame checked all 64 slots, but only flagged ones could ever be
//     written; the port iterates only those (same result, no pawn queries for everyone else).
//   * the original's OnStartupServer (entity-system pointers) is not needed - the port keeps no
//     engine pointers.
//   * besides the original's loaded/disconnect callbacks the port also listens to
//     VIP_OnVIPClientRemoved and VIP_OnPlayerSpawn: the core notifies modules of an expiry or an
//     API removal only through the former, and a group change through neither, so the original's
//     flag stayed set for the rest of the session.

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

// --- map gate (PITFALLS B1/B2) ----------------------------------------------------------------

bool g_mapReady = false;      // every client call is safe right now
bool g_mapStartSeen = false;  // an OnMapStart arrived since OnPluginStart / the last OnMapEnd
bool g_mapEndSeen = false;    // an OnMapEnd arrived since OnPluginStart
bool g_mapListenersRegistered = false;

// s2sdk listeners: plain function pointers called on the game thread. Nothing here may throw and
// no client query is made - only state changes.
void OnMapStartListener() {
	// The new map and its entity system exist: a client call is legal from this point on.
	g_mapReady = true;
	g_mapStartSeen = true;
	g_mapEndSeen = false;
}

void OnMapEndListener() {
	// PITFALLS B2: from here until the next OnMapStart s2sdk's client list is being torn down and
	// any client query crashes - drop the state and nothing else.
	g_mapReady = false;
	g_mapStartSeen = false;
	g_mapEndSeen = true;
}

void InitMapGate() {
	g_mapReady = false;
	g_mapStartSeen = false;
	g_mapEndSeen = false;
	if (g_mapListenersRegistered) {
		return;
	}
	if (__s2sdk_OnMapStart_Register == nullptr || __s2sdk_OnMapEnd_Register == nullptr) {
		// Without the map-end listener nothing could ever take readiness away again, so the gate
		// stays closed for good (PITFALLS B2: log and stay inactive, never return Failed).
		LogTaggedError("the s2sdk map listeners are unavailable - every client call stays refused");
		return;
	}
	s2sdk::OnMapStart_Register(&OnMapStartListener);
	s2sdk::OnMapEnd_Register(&OnMapEndListener);
	g_mapListenersRegistered = true;
}

void PollMapGate() {
	if (!g_mapListenersRegistered) {
		return;
	}
	// A plugin loaded into a running map never saw an OnMapStart: the first update after
	// OnPluginStart is the proof of a live map (the host pumps updates from the server frame).
	// After an OnMapEnd only a new OnMapStart re-opens the gate: the host keeps pumping updates
	// through a changelevel, so `!g_mapEndSeen` must not be enough once one was seen.
	if (!g_mapReady && (g_mapStartSeen || !g_mapEndSeen)) {
		g_mapReady = true;
	}
}

void ShutdownMapGate() {
	if (g_mapListenersRegistered) {
		g_mapListenersRegistered = false;
		if (__s2sdk_OnMapStart_Unregister != nullptr) {
			s2sdk::OnMapStart_Unregister(&OnMapStartListener);
		}
		if (__s2sdk_OnMapEnd_Unregister != nullptr) {
			s2sdk::OnMapEnd_Unregister(&OnMapEndListener);
		}
	}
	g_mapReady = false;
	g_mapStartSeen = false;
	g_mapEndSeen = false;
}

// --- client checks (PITFALLS C2) --------------------------------------------------------------

// The original's CCSPlayerController::FromSlot + m_hPlayerPawn + IsAlive chain. Every s2sdk
// export dereferences the engine's client record without validating it, so the checks run in
// layers and each layer only runs once the previous ones passed.
bool IsAlive(int32_t slot) {
	if (!g_mapReady || slot < 0 || slot >= vip_endurance::kMaxSlots) {
		return false;
	}
	if (__s2sdk_IsClientConnected == nullptr || __s2sdk_IsClientInGame == nullptr ||
		__s2sdk_IsClientSourceTV == nullptr || __s2sdk_PlayerSlotToEntPointer == nullptr ||
		__s2sdk_IsClientAlive == nullptr) {
		return false;
	}
	try {
		return s2sdk::IsClientConnected(slot) && s2sdk::IsClientInGame(slot) &&
			   !s2sdk::IsClientSourceTV(slot) && s2sdk::PlayerSlotToEntPointer(slot) != nullptr &&
			   s2sdk::IsClientAlive(slot);
	} catch (...) {
		return false;
	}
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
	// The original's per-frame body only ran once the core was loaded.
	if (!g_pVIPCore->VIP_IsVIPLoaded()) {
		return;
	}
	for (int32_t i = 0; i < vip_endurance::kMaxSlots; ++i) {
		if (!m_state.enabled[i]) {
			continue;
		}
		// The original's controller + pawn + IsAlive chain; IsAlive also refuses every call
		// outside a live map (PITFALLS B1/B2).
		if (!IsAlive(i)) {
			continue;
		}
		// The original's write: a flagged slot whose velocity modifier is below 1.0 gets it set
		// back to 1.0.
		if (__s2sdk_GetClientSpeed == nullptr || __s2sdk_SetClientSpeed == nullptr) {
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
		// The map listeners come first: they gate every client call this plugin makes (B1/B2).
		InitMapGate();

		// The original registered the client-loaded and client-disconnect callbacks.
		g_pVIPCore->VIP_OnClientLoaded([this](int iSlot, bool bIsVIP) {
			// The flag follows the "Endurance" feature for a VIP, false otherwise.
			vip_endurance::OnClientLoaded(m_state, iSlot, bIsVIP,
										  bIsVIP && g_pVIPCore->VIP_GetClientFeatureBool(iSlot, kFeature));
		});
		g_pVIPCore->VIP_OnClientDisconnect([this](int iSlot, bool /*bIsVIP*/) {
			vip_endurance::OnClientDisconnect(m_state, iSlot);
		});
		// The core fires no client-loaded/disconnect callback when a record expires, is removed
		// through the API or changes group, so these two keep the flag in step:
		// VIP_OnVIPClientRemoved covers the expiry loop and VIP_RemoveClientVIP immediately, and
		// VIP_OnPlayerSpawn re-reads the feature (the group change and a core reload only become
		// visible there).
		g_pVIPCore->VIP_OnVIPClientRemoved([this](int iSlot, int /*iReason*/) {
			vip_endurance::OnClientDisconnect(m_state, iSlot);
		});
		g_pVIPCore->VIP_OnPlayerSpawn([this](int iSlot, int /*iTeam*/, bool bIsVIP) {
			vip_endurance::OnClientLoaded(m_state, iSlot, bIsVIP,
										  bIsVIP && g_pVIPCore->VIP_GetClientFeatureBool(iSlot, kFeature));
		});

		// The original registered the feature with a toggle callback that stores the new status
		// and lets the toggle through.
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
	return {}; // never Failed: that stops every plugify plugin (PITFALLS A1)
}

plg::PluginResult EndurancePlugin::OnPluginUpdate(std::chrono::milliseconds /*dt*/) {
	try {
		// The first update after a late load opens the client-call gate (an OnMapStart opens it
		// immediately, so this is a no-op while a map is up).
		PollMapGate();
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
		g_pVIPCore->VIP_OnVIPClientRemovedUnregister();
		g_pVIPCore->VIP_OnPlayerSpawnUnregister();
		// The vip API has no feature unregister: re-registering the feature without callbacks
		// replaces the core's entry with null delegates, so the core keeps no pointer into this
		// library after it is unloaded (same value/feature type: the menu entry is unchanged).
		g_pVIPCore->VIP_RegisterFeature(kFeature, VIP_BOOL, TOGGLABLE);
	} catch (...) {
		// nothing left to do at unload
	}
	try {
		// The map listeners point into this library: drop them before it is unloaded (A7).
		ShutdownMapGate();
	} catch (...) {
	}
	return {};
}

} // namespace

PLUGIFY_PLUGIN(PLUGIN_API, &g_plugin)
