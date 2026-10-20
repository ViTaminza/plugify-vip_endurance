// vip_endurance unit tests — the engine-free part: the per-slot flag transitions (client loaded
// / disconnect / the menu toggle).
//
// Run by ../../test.sh (inside the Steam Runtime image, cwd = build/tests).

#include "endurance_logic.hpp"

#include <cstdio>
#include <string>

namespace {

int g_checks = 0;
int g_failures = 0;

void CheckEq(bool actual, bool expected, const std::string& what) {
	++g_checks;
	if (actual != expected) {
		++g_failures;
		std::printf("FAIL: %s (expected %d, got %d)\n", what.c_str(), expected ? 1 : 0, actual ? 1 : 0);
	}
}

} // namespace

int main() {
	std::printf("vip_endurance tests\n");

	vip_endurance::State state;

	// A VIP with the feature gets the flag.
	vip_endurance::OnClientLoaded(state, 4, true, true);
	CheckEq(state.enabled[4], true, "VIP with feature enabled");

	// A VIP whose "Endurance" value is missing/0 does not.
	vip_endurance::OnClientLoaded(state, 5, true, false);
	CheckEq(state.enabled[5], false, "VIP without the feature");

	// A non-VIP never does, even if the feature value would be true.
	vip_endurance::OnClientLoaded(state, 6, false, true);
	CheckEq(state.enabled[6], false, "non-VIP");

	// The toggle callback stores the new status.
	vip_endurance::OnToggle(state, 4, false);
	CheckEq(state.enabled[4], false, "toggled off");
	vip_endurance::OnToggle(state, 4, true);
	CheckEq(state.enabled[4], true, "toggled back on");

	// Disconnect clears the flag.
	vip_endurance::OnClientDisconnect(state, 4);
	CheckEq(state.enabled[4], false, "disconnected");

	// Out-of-range slots are ignored.
	vip_endurance::OnClientLoaded(state, 64, true, true);
	vip_endurance::OnClientLoaded(state, -1, true, true);
	vip_endurance::OnToggle(state, 64, true);
	vip_endurance::OnClientDisconnect(state, -1);
	CheckEq(state.enabled[63], false, "out-of-range slots ignored");

	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
