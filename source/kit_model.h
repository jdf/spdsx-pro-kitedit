// The kit: a name and nine pads, each carrying a top and bottom sample.
// This is the source of truth the UI and audio engine react to, the unit
// that undo mutates, and the thing that gets serialized to .kit files.
#ifndef SPDSX_PATCHEDIT_SOURCE_KIT_MODEL_H_
#define SPDSX_PATCHEDIT_SOURCE_KIT_MODEL_H_

#include <algorithm>
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
  // Respond to strike velocity? Off = every hit plays at the fixed
  // velocity below.
  bool dynamics = true;
  // Velocity -> loudness transfer, when dynamics is on.
  DynamicsCurve curve = DynamicsCurve::kLinear;
  // The strike level every hit plays at when dynamics is off (1..127).
  int fixed_velocity = kDefaultFixedVelocity;
  // Device-only (read/write, not emulated): hold a strike until the
  // next click accent instead of sounding immediately.
  bool trigger_reserve = false;
  // The directional pad-link pair: this object SENDS its hits to
  // link_send's group, and RECEIVES hits from link_receive's group;
  // 0 = none. Stored and synced; not emulated by the engine.
  int link_send = 0;
  int link_receive = 0;
  // HI-HAT mode's closed-pedal shaping (0..127). Volume scales the
  // closed layer and is emulated; fade in (attack) and decay are
  // read/write for the device only until the engine grows envelopes.
  int hi_hat_volume = kDefaultHiHatVolume;
  int hi_hat_fade_in = kDefaultHiHatFadeIn;
  int hi_hat_decay = kDefaultHiHatDecay;

  // Per-layer mix (top = layer A, bottom = layer B): volume in 0.1 dB
  // steps (0 = 0.0 dB, negative = quieter), fade-in 0..127, decay 0..127
  // (127 = none). Emulated on pad hits: volume scales the layer's gain,
  // fade-in/decay play through LayerEnvelopeGain (both lerp the
  // sample's total length). Slot-level auditioning ignores all three.
  struct LayerMix {
    int volume_db10 = kDefaultLayerVolumeDb10;
    int fade_in = kDefaultLayerFadeIn;
    int decay = kDefaultLayerDecay;

    bool operator==(const LayerMix&) const = default;
  };

  LayerMix mix_top;
  LayerMix mix_bottom;

  bool operator==(const PadParams&) const = default;
};

// One editable field of PadParams — the granularity at which the
// Properties panel reports an edit, so a change to one control can land
// on several selected pads without disturbing their other fields.
enum class PadParamsField {
  kMode,
  kFadePoint,
  kFadeEnd,
  kDynamics,
  kCurve,
  kFixedVelocity,
  kLinkSend,
  kLinkReceive,
  kMixTopVolume,
  kMixTopFadeIn,
  kMixTopDecay,
  kMixBottomVolume,
  kMixBottomFadeIn,
  kMixBottomDecay,
  kHiHatVolume,
  kHiHatFadeIn,
  kHiHatDecay,
};

// Copies one field of src into dst, leaving everything else alone. The
// fade pair keeps its structural invariant per destination: the end
// never sits below the point.
inline void ApplyPadParamsField(PadParams& dst,
                                const PadParams& src,
                                PadParamsField field) {
  switch (field) {
    case PadParamsField::kMode:
      dst.mode = src.mode;
      break;
    case PadParamsField::kFadePoint:
      dst.fade_point = src.fade_point;
      dst.fade_end = std::max(dst.fade_end, dst.fade_point);
      break;
    case PadParamsField::kFadeEnd:
      dst.fade_end = std::max(dst.fade_point, src.fade_end);
      break;
    case PadParamsField::kDynamics:
      dst.dynamics = src.dynamics;
      break;
    case PadParamsField::kCurve:
      dst.curve = src.curve;
      break;
    case PadParamsField::kFixedVelocity:
      dst.fixed_velocity = src.fixed_velocity;
      break;
    case PadParamsField::kLinkSend:
      dst.link_send = src.link_send;
      break;
    case PadParamsField::kLinkReceive:
      dst.link_receive = src.link_receive;
      break;
    case PadParamsField::kMixTopVolume:
      dst.mix_top.volume_db10 = src.mix_top.volume_db10;
      break;
    case PadParamsField::kMixTopFadeIn:
      dst.mix_top.fade_in = src.mix_top.fade_in;
      break;
    case PadParamsField::kMixTopDecay:
      dst.mix_top.decay = src.mix_top.decay;
      break;
    case PadParamsField::kMixBottomVolume:
      dst.mix_bottom.volume_db10 = src.mix_bottom.volume_db10;
      break;
    case PadParamsField::kMixBottomFadeIn:
      dst.mix_bottom.fade_in = src.mix_bottom.fade_in;
      break;
    case PadParamsField::kMixBottomDecay:
      dst.mix_bottom.decay = src.mix_bottom.decay;
      break;
    case PadParamsField::kHiHatVolume:
      dst.hi_hat_volume = src.hi_hat_volume;
      break;
    case PadParamsField::kHiHatFadeIn:
      dst.hi_hat_fade_in = src.hi_hat_fade_in;
      break;
    case PadParamsField::kHiHatDecay:
      dst.hi_hat_decay = src.hi_hat_decay;
      break;
  }
}

// One layer's assignment: nothing, a local audio file, or a wave in
// the device's sample pool (referenced by pool index). Device waves
// are metadata-only until the audio-transfer protocol is cracked, so
// they display but can't play; at sync time local files get uploaded
// and become pool indices too.
struct LayerSample {
  LayerSample() = default;

  explicit LayerSample(const juce::File& f)
      : file(f) {}

  static LayerSample DeviceWave(int index) {
    LayerSample sample;
    sample.device_index = index;
    return sample;
  }

  bool empty() const { return device_index == 0 && file == juce::File(); }

  bool is_file() const { return device_index == 0 && file != juce::File(); }

  bool is_device() const { return device_index > 0; }

  bool operator==(const LayerSample&) const = default;

  juce::File file;  // the local file, when device_index == 0
  int device_index = 0;  // > 0 refers to the device pool
};

struct Pad {
  // top, bottom; an empty LayerSample means the layer holds nothing.
  std::pair<LayerSample, LayerSample> samples;
  PadParams params;

  bool operator==(const Pad&) const = default;
};

class KitModel {
public:
  // The model holds every triggerable object on the kit: the nine pads
  // (objects 0-8) followed by the eight external trigger inputs
  // (objects 9-16). Triggers are structurally pads — same samples, same
  // params — so everything downstream (undo, engine, sync) treats an
  // object index uniformly; code that means the PHYSICAL pads (MIDI
  // notes, the 3x3 grid) loops to kPadCount.
  static constexpr int kPadCount = 9;
  static constexpr int kTriggerCount = 8;
  static constexpr int kObjectCount = kPadCount + kTriggerCount;
  static constexpr int kLayersPerPad = 2;
  // The UI still sizes its flat slot arrays from this.
  static constexpr int kSlotCount = kObjectCount * kLayersPerPad;

  static bool IsTrigger(int object) { return object >= kPadCount; }

  // Trigger t (0-based) as an object index, and back.
  static int TriggerObject(int trigger) { return kPadCount + trigger; }

  static int TriggerOf(int object) { return object - kPadCount; }

  // Objects start in their default modes (pad 9 is the hi-hat).
  KitModel() {
    for (int i = 0; i < kObjectCount; ++i) {
      pads_[static_cast<size_t>(i)].params = DefaultParams(i);
    }
  }

  // Pad 9 (bottom-right) defaults to HI-HAT, the rest — triggers
  // included — to MIX.
  static PadParams DefaultParams(int pad) {
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

    virtual void SampleChanged(int pad, int layer) {
      (void)pad;
      (void)layer;
    }

    // Layer mode or fade parameters changed.
    virtual void PadParamsChanged(int pad) { (void)pad; }
  };

  const juce::String& name() const { return name_; }

  void set_name(const juce::String& name) {
    if (name_ != name) {
      name_ = name;
      listeners_.call([](Listener& l) { l.KitNameChanged(); });
    }
  }

  const Pad& pad(int idx) const { return pads_.at(static_cast<size_t>(idx)); }

  // layer 0 is the top sample, 1 the bottom.
  const LayerSample& sample(int pad, int layer) const {
    const auto& samples = pads_.at(static_cast<size_t>(pad)).samples;
    return layer == 0 ? samples.first : samples.second;
  }

  void set_sample(int pad, int layer, const LayerSample& sample) {
    auto& samples = pads_.at(static_cast<size_t>(pad)).samples;
    auto& current = layer == 0 ? samples.first : samples.second;
    if (current != sample) {
      current = sample;
      listeners_.call(
          [pad, layer](Listener& l) { l.SampleChanged(pad, layer); });
    }
  }

  const PadParams& params(int pad) const {
    return pads_.at(static_cast<size_t>(pad)).params;
  }

  void SetPadParams(int pad, const PadParams& params) {
    auto& p = pads_.at(static_cast<size_t>(pad));
    if (p.params != params) {
      p.params = params;
      listeners_.call([pad](Listener& l) { l.PadParamsChanged(pad); });
    }
  }

  void AddListener(Listener* listener) { listeners_.add(listener); }

  void RemoveListener(Listener* listener) { listeners_.remove(listener); }

private:
  juce::String name_ {"Untitled Kit"};
  std::array<Pad, kObjectCount> pads_;
  juce::ListenerList<Listener> listeners_;
};

}  // namespace spdsx

#endif  // SPDSX_PATCHEDIT_SOURCE_KIT_MODEL_H_
