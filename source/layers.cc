#include "layers.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace spdsx {

namespace {

constexpr std::array<std::string_view, kLayerModeCount> kNames = {"MIX",
    "FADE1", "FADE2", "XFADE", "SWITCH", "SW(MONO)", "ALTERNATE", "HI-HAT"};

constexpr std::array<std::string_view, kDynamicsCurveCount> kCurveNames = {
    "LINEAR", "LOUD1", "LOUD2", "LOUD3"};

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

float DynamicsGain(DynamicsCurve curve, int velocity)
{
  const float v = static_cast<float>(std::clamp(velocity, 0, 127)) / 127.0f;
  // Power curves: gamma < 1 lifts soft strikes toward full volume,
  // progressively flatter from LOUD1 to LOUD3. Approximations (the
  // real device shapes are unpublished); all agree at 0 and 127.
  switch (curve) {
    case DynamicsCurve::kLinear:
      return v;
    case DynamicsCurve::kLoud1:
      return std::pow(v, 0.65f);
    case DynamicsCurve::kLoud2:
      return std::pow(v, 0.45f);
    case DynamicsCurve::kLoud3:
      return std::pow(v, 0.30f);
  }
  return v;
}

LayerWeights ComputeLayerWeights(LayerMode mode, int velocity,
    int fade_point, int fade_end, bool alternate_flip, bool pedal_down)
{
  switch (mode) {
    case LayerMode::kMix:
      return {1.0f, 1.0f, false};
    case LayerMode::kFade1:
      // B is binary: silent below the fade point, full presence at it.
      return {1.0f, velocity >= fade_point ? 1.0f : 0.0f, false};
    case LayerMode::kFade2:
      return {1.0f, FadeAmount(velocity, fade_point, fade_end), false};
    case LayerMode::kXfade: {
      const float t = FadeAmount(velocity, fade_point, fade_end);
      return {1.0f - t, t, false};
    }
    case LayerMode::kSwitch:
      return velocity < fade_point ? LayerWeights {1.0f, 0.0f, false}
                                   : LayerWeights {0.0f, 1.0f, false};
    case LayerMode::kSwitchMono:
      return velocity < fade_point ? LayerWeights {1.0f, 0.0f, true}
                                   : LayerWeights {0.0f, 1.0f, true};
    case LayerMode::kAlternate:
      return alternate_flip ? LayerWeights {0.0f, 1.0f, false}
                            : LayerWeights {1.0f, 0.0f, false};
    case LayerMode::kHiHat:
      return pedal_down ? LayerWeights {1.0f, 0.0f, false}
                        : LayerWeights {0.0f, 1.0f, false};
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

std::string_view DynamicsCurveName(DynamicsCurve curve)
{
  return kCurveNames.at(static_cast<size_t>(curve));
}

DynamicsCurve ParseDynamicsCurve(std::string_view name,
    DynamicsCurve fallback)
{
  for (size_t i = 0; i < kCurveNames.size(); ++i) {
    if (kCurveNames[i] == name) {
      return static_cast<DynamicsCurve>(i);
    }
  }
  return fallback;
}

}  // namespace spdsx
