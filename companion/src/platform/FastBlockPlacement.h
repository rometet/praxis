#pragma once
class LocalPlayer;
namespace praxis::companion {
void installFastBlockPlacement() noexcept;
void removeFastBlockPlacement() noexcept;
// Deliberately nonpersistent. Must be explicitly enabled after joining
// each world. The normal multiplayer fallback always remains the default.
void setFastBlockPlacementRemoteTrial(bool enabled) noexcept;
bool fastBlockPlacementRemoteTrial() noexcept;
bool fastBlockPlacementRemoteTrialTripped() noexcept;
void tickFastBlockPlacement(LocalPlayer& player) noexcept;
// Opt-in read-only packet sender probe: deferred install only after gameplay.
// Never clones, edits, drops or sends packets.
void setFastBlockPlacementWireProbe(bool enabled) noexcept;
bool fastBlockPlacementWireProbe() noexcept;
bool fastBlockPlacementWireProbeInstalled() noexcept;
bool fastBlockPlacementWireProbeFailed() noexcept;
}
