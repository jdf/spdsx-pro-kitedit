// Pad layer modes: how a pad's two samples (top = A, bottom = B) respond
// to a single hit. Emulates the SPD-SX PRO's eight layer types. The modes
// differ in what selects between the layers: nothing (MIX), velocity as an
// additive threshold (FADE1/FADE2), velocity as a morph (XFADE), velocity
// as a hard selector (SWITCH/SW(MONO)), hit count (ALTERNATE), or the
// hi-hat pedal (HI-HAT).
//
// JUCE-free pure logic, so it can be unit-tested standalone and later
// shared with the device layer.
#ifndef SPDSX_PATCHEDIT_SOURCE_LAYERS_H_
#define SPDSX_PATCHEDIT_SOURCE_LAYERS_H_

#include <string_view>

namespace spdsx {

enum class LayerMode {
  kMix,         // A and B always play together; velocity only sets loudness
  kFade1,       // B joins, at full presence, at/above the fade point
  kFade2,       // B blooms in from the fade point, equal to A at fade end
  kXfade,       // like kFade2 but A trades away as B comes up
  kSwitch,      // A below the fade point, B at/above it, never both
  kSwitchMono,  // kSwitch, but a hit chokes whatever is still ringing
  kAlternate,   // A and B alternate per hit, velocity-independent
  kHiHat,       // pedal down = A (closed), pedal up = B (open)
};

inline constexpr int kLayerModeCount = 8;
inline constexpr int kDefaultFadePoint = 64;
inline constexpr int kDefaultFadeEnd = 127;

// Per-hit result: linear gain for each layer (0 = that layer doesn't
// fire), plus whether the hit chokes anything still ringing on the pad.
struct LayerGains {
  float top = 0.0f;
  float bottom = 0.0f;
  bool choke = false;
};

// velocity and fade_point/fade_end are MIDI-style 1..127. alternate_flip
// is the pad's flip-flop state (false = A fires next); pedal_down is the
// hi-hat pedal. Modes ignore the inputs they don't use.
LayerGains ComputeLayerGains(LayerMode mode, int velocity, int fade_point,
    int fade_end, bool alternate_flip, bool pedal_down);

// True when the mode reads the fade point (and, for the blended modes,
// the fade end); drives which controls the UI shows.
bool UsesFadePoint(LayerMode mode);
bool UsesFadeEnd(LayerMode mode);

// Stable names, used in the UI and in .kit files.
std::string_view LayerModeName(LayerMode mode);
LayerMode ParseLayerMode(std::string_view name, LayerMode fallback);

}  // namespace spdsx

#endif  // SPDSX_PATCHEDIT_SOURCE_LAYERS_H_
