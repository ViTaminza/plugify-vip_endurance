// vip_endurance tests — no engine: the plugin TU is compiled in (its map gate and client checks
// live in an anonymous namespace) and every __s2sdk_* / __vip_* binding slot is a recording fake.
//
// Covered:
//   * PITFALLS B1/B2 — the map gate: not one s2sdk client call before the first map, in the
//     window between OnMapEnd and the next OnMapStart, or after OnPluginEnd; the first update
//     latches readiness only when no OnMapEnd was seen since OnPluginStart (a plugin loaded into
//     the map-change window); OnMapStart re-opens the gate; without the map listeners the gate
//     never opens at all;
//   * PITFALLS C2 — IsAlive: the slot range, connected / in-game / SourceTV / controller / alive
//     layers, each checked before the next export is called;
//   * the per-frame write of the original's GameFrame: only a flagged, alive slot whose velocity
//     modifier is below 1.0 is set back to 1.0, and nothing is written while no map is up;
//   * the per-slot flag transitions of the original module (loaded / disconnect / toggle);
//   * a clean unload (PITFALLS A7): the map and VIP listeners are unregistered and the feature's
//     callbacks are replaced, then a reload registers everything again exactly once.
//
// Run by ./test.sh (build/tests/vip_endurance_tests).

#include "../src/plugin.cpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <string>

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool condition, const std::string& what) {
	++g_checks;
	if (!condition) {
		++g_failures;
		std::printf("FAIL: %s\n", what.c_str());
	}
}

// --- fake s2sdk client exports ---------------------------------------------------------------

struct FakeClient {
	bool connected = true;
	bool inGame = true;
	bool sourceTV = false;
	bool alive = true;
	void* controller = reinterpret_cast<void*>(0x1000);
};

FakeClient g_clients[vip_endurance::kMaxSlots];

int g_connectedCalls = 0;
int g_inGameCalls = 0;
int g_sourceTVCalls = 0;
int g_controllerCalls = 0;
int g_aliveCalls = 0;
int g_badSlotCalls = 0;

float g_speeds[vip_endurance::kMaxSlots]{};
int g_speedGets = 0;
int g_speedSets = 0;
int32_t g_lastSpeedSlot = -1;
float g_lastSpeedValue = 0.0f;

bool InRange(int32_t slot) {
	if (slot >= 0 && slot < vip_endurance::kMaxSlots) {
		return true;
	}
	++g_badSlotCalls;
	return false;
}

void ResetClientCounters() {
	g_connectedCalls = 0;
	g_inGameCalls = 0;
	g_sourceTVCalls = 0;
	g_controllerCalls = 0;
	g_aliveCalls = 0;
	g_badSlotCalls = 0;
}

int ClientCalls() {
	return g_connectedCalls + g_inGameCalls + g_sourceTVCalls + g_controllerCalls + g_aliveCalls;
}

void ResetSpeedCounters() {
	g_speedGets = 0;
	g_speedSets = 0;
	g_lastSpeedSlot = -1;
	g_lastSpeedValue = 0.0f;
}

bool FakeIsClientConnected(int32_t slot) {
	++g_connectedCalls;
	return InRange(slot) && g_clients[slot].connected;
}

bool FakeIsClientInGame(int32_t slot) {
	++g_inGameCalls;
	return InRange(slot) && g_clients[slot].inGame;
}

bool FakeIsClientSourceTV(int32_t slot) {
	++g_sourceTVCalls;
	return InRange(slot) && g_clients[slot].sourceTV;
}

void* FakePlayerSlotToEntPointer(int32_t slot) {
	++g_controllerCalls;
	return InRange(slot) ? g_clients[slot].controller : nullptr;
}

bool FakeIsClientAlive(int32_t slot) {
	++g_aliveCalls;
	return InRange(slot) && g_clients[slot].alive;
}

float FakeGetClientSpeed(int32_t slot) {
	++g_speedGets;
	return InRange(slot) ? g_speeds[slot] : 0.0f;
}

void FakeSetClientSpeed(int32_t slot, float speed) {
	++g_speedSets;
	if (!InRange(slot)) {
		return;
	}
	g_lastSpeedSlot = slot;
	g_lastSpeedValue = speed;
	g_speeds[slot] = speed;
}

// --- fake s2sdk map listeners ----------------------------------------------------------------

s2sdk::OnMapStartCallback g_onMapStart = nullptr;
s2sdk::OnMapEndCallback g_onMapEnd = nullptr;
int g_mapStartRegisters = 0;
int g_mapStartUnregisters = 0;
int g_mapEndRegisters = 0;
int g_mapEndUnregisters = 0;

void FakeMapStartRegister(s2sdk::OnMapStartCallback callback) {
	++g_mapStartRegisters;
	g_onMapStart = callback;
}

void FakeMapStartUnregister(s2sdk::OnMapStartCallback callback) {
	++g_mapStartUnregisters;
	if (g_onMapStart == callback) {
		g_onMapStart = nullptr;
	}
}

void FakeMapEndRegister(s2sdk::OnMapEndCallback callback) {
	++g_mapEndRegisters;
	g_onMapEnd = callback;
}

void FakeMapEndUnregister(s2sdk::OnMapEndCallback callback) {
	++g_mapEndUnregisters;
	if (g_onMapEnd == callback) {
		g_onMapEnd = nullptr;
	}
}

void MapStart() {
	if (g_onMapStart != nullptr) {
		g_onMapStart();
	}
}

void MapEnd() {
	if (g_onMapEnd != nullptr) {
		g_onMapEnd();
	}
}

// --- fake vip core exports -------------------------------------------------------------------

bool g_vipLoaded = true;
bool g_vipFeatureEnabled[vip_endurance::kMaxSlots]{};
int g_vipRegisterFeatureCalls = 0;

vip::ClientLoadedOrDisconnectDelegate g_onClientLoaded = nullptr;
vip::ClientLoadedOrDisconnectDelegate g_onClientDisconnect = nullptr;
vip::VipRemoveDelegate g_onVipClientRemoved = nullptr;
vip::SpawnDelegate g_onPlayerSpawn = nullptr;
vip::ItemTogglableDelegate g_featureToggle = nullptr;

bool FakeVipIsLoaded() {
	return g_vipLoaded;
}

bool FakeVipGetClientFeatureBool(int32_t slot, const plg::string& /*feature*/) {
	return slot >= 0 && slot < vip_endurance::kMaxSlots && g_vipFeatureEnabled[slot];
}

void FakeVipOnClientLoadedRegister(vip::ClientLoadedOrDisconnectDelegate callback) {
	g_onClientLoaded = callback;
}

void FakeVipOnClientLoadedUnregister(vip::ClientLoadedOrDisconnectDelegate callback) {
	if (g_onClientLoaded == callback) {
		g_onClientLoaded = nullptr;
	}
}

void FakeVipOnClientDisconnectRegister(vip::ClientLoadedOrDisconnectDelegate callback) {
	g_onClientDisconnect = callback;
}

void FakeVipOnClientDisconnectUnregister(vip::ClientLoadedOrDisconnectDelegate callback) {
	if (g_onClientDisconnect == callback) {
		g_onClientDisconnect = nullptr;
	}
}

void FakeVipOnVipClientRemovedRegister(vip::VipRemoveDelegate callback) {
	g_onVipClientRemoved = callback;
}

void FakeVipOnVipClientRemovedUnregister(vip::VipRemoveDelegate callback) {
	if (g_onVipClientRemoved == callback) {
		g_onVipClientRemoved = nullptr;
	}
}

void FakeVipOnPlayerSpawnRegister(vip::SpawnDelegate callback) {
	g_onPlayerSpawn = callback;
}

void FakeVipOnPlayerSpawnUnregister(vip::SpawnDelegate callback) {
	if (g_onPlayerSpawn == callback) {
		g_onPlayerSpawn = nullptr;
	}
}

void FakeVipRegisterFeature(const plg::string& /*feature*/, vip::ValueType /*valueType*/,
							vip::FeatureType /*featureType*/, vip::ItemSelectableDelegate /*select*/,
							vip::ItemTogglableDelegate toggle, vip::ItemDisplayDelegate /*display*/) {
	++g_vipRegisterFeatureCalls;
	// The plugin registers the feature with its toggle callback on start and replaces the entry
	// with null callbacks on unload, so the last value is the one the core would hold.
	g_featureToggle = toggle;
}

// --- the fake world --------------------------------------------------------------------------

void InstallFakes() {
	__s2sdk_OnMapStart_Register = &FakeMapStartRegister;
	__s2sdk_OnMapStart_Unregister = &FakeMapStartUnregister;
	__s2sdk_OnMapEnd_Register = &FakeMapEndRegister;
	__s2sdk_OnMapEnd_Unregister = &FakeMapEndUnregister;

	__s2sdk_IsClientConnected = &FakeIsClientConnected;
	__s2sdk_IsClientInGame = &FakeIsClientInGame;
	__s2sdk_IsClientSourceTV = &FakeIsClientSourceTV;
	__s2sdk_PlayerSlotToEntPointer = &FakePlayerSlotToEntPointer;
	__s2sdk_IsClientAlive = &FakeIsClientAlive;
	__s2sdk_GetClientSpeed = &FakeGetClientSpeed;
	__s2sdk_SetClientSpeed = &FakeSetClientSpeed;

	__vip_VIP_IsVIPLoaded = &FakeVipIsLoaded;
	__vip_VIP_GetClientFeatureBool = &FakeVipGetClientFeatureBool;
	__vip_VIP_OnClientLoaded_Register = &FakeVipOnClientLoadedRegister;
	__vip_VIP_OnClientLoaded_Unregister = &FakeVipOnClientLoadedUnregister;
	__vip_VIP_OnClientDisconnect_Register = &FakeVipOnClientDisconnectRegister;
	__vip_VIP_OnClientDisconnect_Unregister = &FakeVipOnClientDisconnectUnregister;
	__vip_VIP_OnVIPClientRemoved_Register = &FakeVipOnVipClientRemovedRegister;
	__vip_VIP_OnVIPClientRemoved_Unregister = &FakeVipOnVipClientRemovedUnregister;
	__vip_VIP_OnPlayerSpawn_Register = &FakeVipOnPlayerSpawnRegister;
	__vip_VIP_OnPlayerSpawn_Unregister = &FakeVipOnPlayerSpawnUnregister;
	__vip_VIP_RegisterFeature = &FakeVipRegisterFeature;
}

// Forgets every fake registration and all recorded state. The plugin must have been stopped first
// (PrepareTest/FinishTest do that), otherwise it still believes its listeners are registered.
void ResetWorld() {
	for (FakeClient& client : g_clients) {
		client = FakeClient{};
	}
	ResetClientCounters();

	std::fill(std::begin(g_speeds), std::end(g_speeds), 0.0f);
	ResetSpeedCounters();

	g_onMapStart = nullptr;
	g_onMapEnd = nullptr;
	g_mapStartRegisters = 0;
	g_mapStartUnregisters = 0;
	g_mapEndRegisters = 0;
	g_mapEndUnregisters = 0;

	g_vipLoaded = true;
	std::fill(std::begin(g_vipFeatureEnabled), std::end(g_vipFeatureEnabled), false);
	g_vipRegisterFeatureCalls = 0;
	g_onClientLoaded = nullptr;
	g_onClientDisconnect = nullptr;
	g_onVipClientRemoved = nullptr;
	g_onPlayerSpawn = nullptr;
	g_featureToggle = nullptr;
}

// Stops whatever a previous test left behind (OnPluginEnd is a no-op when nothing is registered)
// and starts the next test from an empty fake world.
void PrepareTest() {
	g_plugin.OnPluginEnd();
	ResetWorld();
}

void FinishTest() {
	g_plugin.OnPluginEnd();
	ResetWorld();
}

// --- A. the map gate (PITFALLS B1/B2) ----------------------------------------------------------

void TestMapGate() {
	PrepareTest();
	Check(g_plugin.OnPluginStart().GetCode() == plg::PluginCode::Ok,
		  "OnPluginStart returns Ok (never Failed)");
	Check(g_mapStartRegisters == 1 && g_mapEndRegisters == 1,
		  "OnPluginStart registers both map listeners");
	Check(!g_mapReady, "the gate is shut on plugin start");

	// --- before the first map: not one client call may reach s2sdk ----------------------------
	Check(!IsAlive(3), "IsAlive is false before the first map");
	Check(ClientCalls() == 0, "not one s2sdk client call is made before the first map");

	// A plugin loaded into a running map: its first update latches readiness.
	g_plugin.OnPluginUpdate(std::chrono::milliseconds(0));
	Check(g_mapReady, "the first OnPluginUpdate latches the gate");
	Check(IsAlive(3), "a live client is alive after the latch");
	Check(ClientCalls() > 0, "the client calls reach s2sdk after the latch");

	// --- after OnMapEnd: not one client call may reach s2sdk ---------------------------------
	MapEnd();
	Check(!g_mapReady, "OnMapEnd drops readiness");
	g_plugin.OnPluginUpdate(std::chrono::milliseconds(0));
	Check(!g_mapReady, "an update after OnMapEnd does not re-open the gate");
	ResetClientCounters();
	Check(!IsAlive(3), "IsAlive is false after OnMapEnd");
	Check(ClientCalls() == 0, "not one s2sdk client call is made after OnMapEnd");

	// --- the next OnMapStart opens the gate again without waiting for an update --------------
	MapStart();
	Check(g_mapReady, "OnMapStart latches the gate");
	Check(IsAlive(3), "the client is alive again");

	FinishTest();
}

// A plugin loaded during the map change (OnMapEnd before its first update) must not latch on the
// update: the first update only counts when no OnMapEnd was seen since OnPluginStart.
void TestMapChangeWindowStart() {
	PrepareTest();
	g_plugin.OnPluginStart();
	MapEnd();
	g_plugin.OnPluginUpdate(std::chrono::milliseconds(0));
	Check(!g_mapReady, "the first update after an OnMapEnd does not latch the gate");
	Check(!IsAlive(2), "no client is alive in the map-change window");
	Check(ClientCalls() == 0, "not one s2sdk client call is made in the map-change window");

	MapStart();
	Check(g_mapReady, "the next OnMapStart opens the gate");
	Check(IsAlive(2), "the client is alive after the map start");

	FinishTest();
}

// Without the map listeners nothing could ever close the gate again, so it must never open.
void TestNoMapListeners() {
	PrepareTest();
	const s2sdk::_OnMapStart_Register savedStart = __s2sdk_OnMapStart_Register;
	const s2sdk::_OnMapEnd_Register savedEnd = __s2sdk_OnMapEnd_Register;
	__s2sdk_OnMapStart_Register = nullptr;
	__s2sdk_OnMapEnd_Register = nullptr;

	g_plugin.OnPluginStart();
	Check(!g_mapListenersRegistered, "without the map listeners the gate is never armed");
	g_plugin.OnPluginUpdate(std::chrono::milliseconds(0));
	Check(!g_mapReady, "an update cannot open a gate that has no map-end listener");
	Check(!IsAlive(0), "no client is alive without the map listeners");
	Check(ClientCalls() == 0, "not one s2sdk client call is made without the map listeners");

	g_plugin.OnPluginEnd();
	Check(!g_mapListenersRegistered, "OnPluginEnd leaves the unarmed gate alone");

	__s2sdk_OnMapStart_Register = savedStart;
	__s2sdk_OnMapEnd_Register = savedEnd;
	FinishTest();
}

// --- B. the client checks (PITFALLS C2) -------------------------------------------------------

void TestClientGuards() {
	PrepareTest();
	g_plugin.OnPluginStart();
	MapStart();
	Check(g_mapReady, "the map gate is open");

	// --- the slot range is checked before any export ------------------------------------------
	ResetClientCounters();
	Check(!IsAlive(-1), "slot -1 is refused");
	Check(!IsAlive(vip_endurance::kMaxSlots), "slot 64 is refused");
	Check(!IsAlive(1000), "a far out-of-range slot is refused");
	Check(ClientCalls() == 0, "no s2sdk call is made for an out-of-range slot");
	Check(g_badSlotCalls == 0, "no export ever receives an out-of-range slot");

	// --- every layer in order ------------------------------------------------------------------
	Check(IsAlive(7), "a connected, in-game, alive client with a controller is alive");

	g_clients[7].connected = false;
	ResetClientCounters();
	Check(!IsAlive(7), "a disconnected slot is not alive");
	Check(g_connectedCalls == 1 && g_inGameCalls == 0 && g_sourceTVCalls == 0 &&
			  g_controllerCalls == 0 && g_aliveCalls == 0,
		  "IsClientInGame and the layers below it are not reached for a disconnected slot");

	g_clients[7].connected = true;
	g_clients[7].inGame = false;
	ResetClientCounters();
	Check(!IsAlive(7), "a client outside the game is not alive");
	Check(g_connectedCalls == 1 && g_inGameCalls == 1 && g_sourceTVCalls == 0 &&
			  g_controllerCalls == 0 && g_aliveCalls == 0,
		  "SourceTV and the layers below it are not reached for a client outside the game");

	g_clients[7].inGame = true;
	g_clients[7].sourceTV = true;
	ResetClientCounters();
	Check(!IsAlive(7), "the SourceTV bot is not a player");
	Check(g_connectedCalls == 1 && g_inGameCalls == 1 && g_sourceTVCalls == 1 &&
			  g_controllerCalls == 0 && g_aliveCalls == 0,
		  "the controller is not queried for SourceTV");

	g_clients[7].sourceTV = false;
	g_clients[7].controller = nullptr;
	ResetClientCounters();
	Check(!IsAlive(7), "a client without a controller is not alive");
	Check(g_controllerCalls == 1 && g_aliveCalls == 0,
		  "IsClientAlive is not reached without a controller");

	g_clients[7].controller = reinterpret_cast<void*>(0x1000);
	g_clients[7].alive = false;
	ResetClientCounters();
	Check(!IsAlive(7), "a dead client is not alive");
	Check(g_aliveCalls == 1, "the alive check is reached for a valid client");

	FinishTest();
}

// --- C. the per-frame write (the original's GameFrame) ----------------------------------------

void TestPerFrameWrite() {
	PrepareTest();
	g_vipLoaded = true;
	g_vipFeatureEnabled[3] = true; // VIP with the feature, alive and slowed down
	g_vipFeatureEnabled[4] = true; // VIP with the feature, dead
	g_vipFeatureEnabled[5] = true; // VIP with the feature, alive and not slowed down
	g_speeds[3] = 0.5f;
	g_speeds[5] = 1.0f;
	g_clients[4].alive = false;

	g_plugin.OnPluginStart();
	Check(g_onClientLoaded != nullptr, "OnPluginStart registers the VIP_OnClientLoaded listener");
	Check(g_onClientDisconnect != nullptr, "OnPluginStart registers the VIP_OnClientDisconnect listener");
	Check(g_onVipClientRemoved != nullptr, "OnPluginStart registers the VIP_OnVIPClientRemoved listener");
	Check(g_onPlayerSpawn != nullptr, "OnPluginStart registers the VIP_OnPlayerSpawn listener");
	Check(g_featureToggle != nullptr, "OnPluginStart registers the Endurance toggle callback");
	if (g_onClientLoaded == nullptr || g_featureToggle == nullptr) {
		FinishTest();
		return;
	}

	// The core reports three VIPs with the feature on.
	g_onClientLoaded(3, true);
	g_onClientLoaded(4, true);
	g_onClientLoaded(5, true);

	MapStart();
	Check(g_mapReady, "the map gate is open");

	ResetSpeedCounters();
	g_plugin.OnPluginUpdate(std::chrono::milliseconds(0));
	Check(g_speedGets == 2, "the per-frame pass reads the speed of the two alive flagged slots");
	Check(g_speedSets == 1, "only the slot below 1.0 is rewritten");
	Check(g_lastSpeedSlot == 3 && g_lastSpeedValue == 1.0f,
		  "the slowed-down slot is set back to 1.0");

	// Between maps the per-frame pass must not write anything (PITFALLS B2).
	MapEnd();
	ResetSpeedCounters();
	g_plugin.OnPluginUpdate(std::chrono::milliseconds(0));
	Check(g_speedGets == 0 && g_speedSets == 0, "no speed call between maps");

	// Drop the flags so the global plugin object is clean for the next test.
	if (g_onClientDisconnect != nullptr) {
		g_onClientDisconnect(3, true);
		g_onClientDisconnect(4, true);
		g_onClientDisconnect(5, true);
	}
	FinishTest();
}

// --- D. a clean unload and a reload (PITFALLS A7) ---------------------------------------------

void TestUnload() {
	PrepareTest();
	g_plugin.OnPluginStart();
	MapStart();
	g_vipFeatureEnabled[3] = true;
	Check(IsAlive(3), "the client is alive before the unload");

	g_plugin.OnPluginEnd();
	Check(!g_mapReady, "OnPluginEnd drops readiness");
	Check(g_mapStartUnregisters == 1 && g_mapEndUnregisters == 1,
		  "OnPluginEnd unregisters both map listeners");
	Check(g_onMapStart == nullptr && g_onMapEnd == nullptr,
		  "the map callbacks are gone from s2sdk");
	Check(g_onClientLoaded == nullptr && g_onClientDisconnect == nullptr &&
			  g_onVipClientRemoved == nullptr && g_onPlayerSpawn == nullptr,
		  "OnPluginEnd unregisters every VIP listener");
	Check(g_vipRegisterFeatureCalls == 2 && g_featureToggle == nullptr,
		  "OnPluginEnd replaces the feature entry with null callbacks");

	ResetClientCounters();
	Check(!IsAlive(3), "no client is alive after the unload");
	Check(ClientCalls() == 0, "a check after OnPluginEnd makes no client call");

	// plg reload: a fresh start after the end re-registers everything exactly once.
	g_plugin.OnPluginStart();
	Check(g_mapStartRegisters == 2 && g_mapEndRegisters == 2,
		  "a reload registers the map listeners again");
	Check(g_onClientLoaded != nullptr && g_featureToggle != nullptr,
		  "a reload registers the VIP listener and the toggle callback again");
	Check(!g_mapReady, "a reload starts with the gate shut");
	MapStart();
	Check(g_mapReady, "the reloaded plugin opens the gate on OnMapStart");
	Check(IsAlive(3), "the client is alive after the reload");

	FinishTest();
}

// --- E. the per-slot flag transitions of the original module ----------------------------------

void TestFlagTransitions() {
	vip_endurance::State state;

	// A VIP with the feature gets the flag.
	vip_endurance::OnClientLoaded(state, 4, true, true);
	Check(state.enabled[4], "VIP with feature enabled");

	// A VIP whose "Endurance" value is missing/0 does not.
	vip_endurance::OnClientLoaded(state, 5, true, false);
	Check(!state.enabled[5], "VIP without the feature");

	// A non-VIP never does, even if the feature value would be true.
	vip_endurance::OnClientLoaded(state, 6, false, true);
	Check(!state.enabled[6], "non-VIP");

	// The toggle callback stores the new status.
	vip_endurance::OnToggle(state, 4, false);
	Check(!state.enabled[4], "toggled off");
	vip_endurance::OnToggle(state, 4, true);
	Check(state.enabled[4], "toggled back on");

	// Disconnect clears the flag.
	vip_endurance::OnClientDisconnect(state, 4);
	Check(!state.enabled[4], "disconnected");

	// An expiry/API removal fires no disconnect: the module maps VIP_OnVIPClientRemoved onto
	// OnClientDisconnect, so the same call clears the flag of a VIP who is still online.
	vip_endurance::OnClientLoaded(state, 7, true, true);
	Check(state.enabled[7], "VIP with feature enabled before the expiry");
	vip_endurance::OnClientDisconnect(state, 7);
	Check(!state.enabled[7], "expiry/removal clears the flag");

	// The spawn re-evaluation (VIP_OnPlayerSpawn) uses OnClientLoaded with the current status, so
	// a group change or a reload is picked up without a reconnect.
	vip_endurance::OnClientLoaded(state, 7, false, false);
	Check(!state.enabled[7], "spawn re-evaluation keeps a non-VIP off");

	// Out-of-range slots are ignored.
	vip_endurance::OnClientLoaded(state, vip_endurance::kMaxSlots, true, true);
	vip_endurance::OnClientLoaded(state, -1, true, true);
	vip_endurance::OnToggle(state, vip_endurance::kMaxSlots, true);
	vip_endurance::OnClientDisconnect(state, -1);
	Check(!state.enabled[vip_endurance::kMaxSlots - 1], "out-of-range slots ignored");
}

} // namespace

int main() {
	std::printf("vip_endurance tests (fake s2sdk/vip bindings, no engine)\n");

	InstallFakes();

	TestMapGate();
	TestMapChangeWindowStart();
	TestNoMapListeners();
	TestClientGuards();
	TestPerFrameWrite();
	TestUnload();
	TestFlagTransitions();

	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
