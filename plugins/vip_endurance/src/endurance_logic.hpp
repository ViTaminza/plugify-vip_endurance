// vip_endurance — engine-free part of the Endurance module: the per-slot flag of the original
// (vip_endurance.cpp: the per-slot flag and its three writers).
//
// Kept pure so the transitions are unit-testable (tests/vip_endurance_tests.cpp).

#pragma once

namespace vip_endurance {

constexpr int kMaxSlots = 64;

// g_bEndurance[64] of the original.
struct State {
	bool enabled[kMaxSlots]{};
};

// OnClientLoaded of the original: the flag follows the "Endurance" feature for a VIP, and is
// cleared for everyone else (a missing group value answers false).
inline void OnClientLoaded(State& state, int iSlot, bool bIsVIP, bool bFeatureEnabled) {
	if (iSlot < 0 || iSlot >= kMaxSlots) {
		return;
	}
	state.enabled[iSlot] = bIsVIP && bFeatureEnabled;
}

// OnClientDisconnect of the original.
inline void OnClientDisconnect(State& state, int iSlot) {
	if (iSlot < 0 || iSlot >= kMaxSlots) {
		return;
	}
	state.enabled[iSlot] = false;
}

// OnTogglable of the original: the new menu status is stored, the toggle is not vetoed.
inline void OnToggle(State& state, int iSlot, bool bNewEnabled) {
	if (iSlot < 0 || iSlot >= kMaxSlots) {
		return;
	}
	state.enabled[iSlot] = bNewEnabled;
}

} // namespace vip_endurance
