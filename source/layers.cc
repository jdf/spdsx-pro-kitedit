#include "layers.h"

#include <algorithm>
#include <array>
#include <cstddef>

namespace spdsx {

namespace {

constexpr std::array<std::string_view, kLayerModeCount> kNames = {"MIX",
    "FADE1", "FADE2", "XFADE", "SWITCH", "SW(MONO)", "ALTERNATE", "HI-HAT"};

// 0 at the fade point, 1 at the fade end, clamped; a degenerate range
// (end <= point) behaves as a threshold at the fade point.
float FadeAmount(int velocity, int fade_point, int fade_end)
{
  if (fade_end <= fade_point) {
    return velocity >= fade_point ? 1.0f : 0.0f;
  }
  const float t = static_cast<float>(velocity - fade_point)
      / static_cast<float>(fade_end - fade_point);
  return std::clamp(t, 0.0f, 1.0f);
}

}  // namespace

LayerGains ComputeLayerGains(LayerMode mode, int velocity, int fade_point,
    int fade_end, bool alternate_flip, bool pedal_down)
{
  const float v = static_cast<float>(std::clamp(velocity, 0, 127)) / 127.0f;
  switch (mode) {
    case LayerMode::kMix:
      return {v, v, false};
    case LayerMode::kFade1:
      // B is binary: silent below the fade point, full presence at it.
      return {v, velocity >= fade_point ? v : 0.0f, false};
    case LayerMode::kFade2:
      return {v, FadeAmount(velocity, fade_point, fade_end) * v, false};
    case LayerMode::kXfade: {
      const float t = FadeAmount(velocity, fade_point, fade_end);
      return {(1.0f - t) * v, t * v, false};
    }
    case LayerMode::kSwitch:
      return velocity < fade_point ? LayerGains {v, 0.0f, false}
                                   : LayerGains {0.0f, v, false};
    case LayerMode::kSwitchMono:
      return velocity < fade_point ? LayerGains {v, 0.0f, true}
                                   : LayerGains {0.0f, v, true};
    case LayerMode::kAlternate:
      return alternate_flip ? LayerGains {0.0f, v, false}
                            : LayerGains {v, 0.0f, false};
    case LayerMode::kHiHat:
      return pedal_down ? LayerGains {v, 0.0f, false}
                        : LayerGains {0.0f, v, false};
  }
  return {};
}

bool UsesFadePoint(LayerMode mode)
{
  switch (mode) {
    case LayerMode::kFade1:
    case LayerMode::kFade2:
    case LayerMode::kXfade:
    case LayerMode::kSwitch:
    case LayerMode::kSwitchMono:
      return true;
    default:
      return false;
  }
}

bool UsesFadeEnd(LayerMode mode)
{
  return mode == LayerMode::kFade2 || mode == LayerMode::kXfade;
}

std::string_view LayerModeName(LayerMode mode)
{
  return kNames.at(static_cast<size_t>(mode));
}

LayerMode ParseLayerMode(std::string_view name, LayerMode fallback)
{
  for (size_t i = 0; i < kNames.size(); ++i) {
    if (kNames[i] == name) {
      return static_cast<LayerMode>(i);
    }
  }
  return fallback;
}

}  // namespace spdsx
