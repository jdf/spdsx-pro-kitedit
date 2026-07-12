// The kit: a name and nine pads, each carrying a top and bottom sample.
// This is the source of truth the UI and audio engine react to, the unit
// that undo mutates, and the thing that gets serialized to .kit files.
#ifndef SPDSX_PATCHEDIT_SOURCE_KIT_MODEL_H_
#define SPDSX_PATCHEDIT_SOURCE_KIT_MODEL_H_

#include <array>
#include <utility>

#include <juce_core/juce_core.h>

#include "layers.h"

namespace spdsx {

// Everything about how a pad responds to a hit (as opposed to which
// samples it holds). Mutated as a unit: one undo step, one listener
// notification, however many fields a gesture changes.
struct PadParams {
  // How the two layers split a hit, and the velocity thresholds the
  // fade/switch modes read (1..127).
  LayerMode mode = LayerMode::kMix;
  int fade_point = kDefaultFadePoint;
  int fade_end = kDefaultFadeEnd;
  // Respond to strike velocity? Off = fixed full volume.
  bool dynamics = true;
  // Velocity -> loudness transfer, when dynamics is on.
  DynamicsCurve curve = DynamicsCurve::kLinear;
  // Device-only (read/write, not emulated): hold a strike until the
  // next click accent instead of sounding immediately.
  bool trigger_reserve = false;

  bool operator==(const PadParams&) const = default;
};

struct Pad {
  // top, bottom; an empty File means the layer holds no sample.
  std::pair<juce::File, juce::File> samples;
  PadParams params;
};

class KitModel {
public:
  static constexpr int kPadCount = 9;
  static constexpr int kLayersPerPad = 2;
  // The UI still sizes its flat slot arrays from this.
  static constexpr int kSlotCount = kPadCount * kLayersPerPad;

  // Pads start in their default modes (pad 9 is the hi-hat).
  KitModel()
  {
    for (int i = 0; i < kPadCount; ++i) {
      pads_[static_cast<size_t>(i)].params = DefaultParams(i);
    }
  }

  // Pad 9 (bottom-right) defaults to HI-HAT, the rest to MIX.
  static PadParams DefaultParams(int pad)
  {
    PadParams params;
    if (pad == kPadCount - 1) {
      params.mode = LayerMode::kHiHat;
    }
    return params;
  }

  class Listener {
  public:
    virtual ~Listener() = default;
    virtual void KitNameChanged() {}
    virtual void SampleChanged(int pad, int layer)
    {
      (void)pad;
      (void)layer;
    }
    // Layer mode or fade parameters changed.
    virtual void PadParamsChanged(int pad) { (void)pad; }
  };

  const juce::String& name() const { return name_; }
  void set_name(const juce::String& name)
  {
    if (name_ != name) {
      name_ = name;
      listeners_.call([](Listener& l) { l.KitNameChanged(); });
    }
  }

  const Pad& pad(int idx) const
  {
    return pads_.at(static_cast<size_t>(idx));
  }

  // layer 0 is the top sample, 1 the bottom.
  const juce::File& sample(int pad, int layer) const
  {
    const auto& samples = pads_.at(static_cast<size_t>(pad)).samples;
    return layer == 0 ? samples.first : samples.second;
  }
  void set_sample(int pad, int layer, const juce::File& file)
  {
    auto& samples = pads_.at(static_cast<size_t>(pad)).samples;
    auto& current = layer == 0 ? samples.first : samples.second;
    if (current != file) {
      current = file;
      listeners_.call(
          [pad, layer](Listener& l) { l.SampleChanged(pad, layer); });
    }
  }

  const PadParams& params(int pad) const
  {
    return pads_.at(static_cast<size_t>(pad)).params;
  }
  void SetPadParams(int pad, const PadParams& params)
  {
    auto& p = pads_.at(static_cast<size_t>(pad));
    if (p.params != params) {
      p.params = params;
      listeners_.call([pad](Listener& l) { l.PadParamsChanged(pad); });
    }
  }

  void AddListener(Listener* listener) { listeners_.add(listener); }
  void RemoveListener(Listener* listener)
  {
    listeners_.remove(listener);
  }

private:
  juce::String name_ {"Untitled Kit"};
  std::array<Pad, kPadCount> pads_;
  juce::ListenerList<Listener> listeners_;
};

}  // namespace spdsx

#endif  // SPDSX_PATCHEDIT_SOURCE_KIT_MODEL_H_
