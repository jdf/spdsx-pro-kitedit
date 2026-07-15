#include "layers.h"

#include <array>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

namespace spdsx {
namespace {

constexpr std::array<LayerMode, kLayerModeCount> kAllModes = {
    LayerMode::kMix,
    LayerMode::kFade1,
    LayerMode::kFade2,
    LayerMode::kXfade,
    LayerMode::kSwitch,
    LayerMode::kSwitchMono,
    LayerMode::kAlternate,
    LayerMode::kHiHat};

constexpr std::array<DynamicsCurve, kDynamicsCurveCount> kAllCurves = {
    DynamicsCurve::kLinear,
    DynamicsCurve::kLoud1,
    DynamicsCurve::kLoud2,
    DynamicsCurve::kLoud3};

// A fade band with room to ramp inside it, so a midpoint is exact.
constexpr int kPoint = 80;
constexpr int kEnd = 120;
constexpr int kMid = 100;

LayerWeights Weights(LayerMode mode, int velocity) {
  return ComputeLayerWeights(mode, velocity, kPoint, kEnd, false, false);
}

// ---- The enums are device data ----

// The enumerator order IS the device's stored byte values (kit record +0x00,
// and the DT1 param the writes send). Device reads cast the raw byte straight
// to this enum, so reordering it silently corrupts every kit read and written.
TEST(LayerMode, EnumeratorsAreTheDeviceByteValues) {
  EXPECT_EQ(static_cast<int>(LayerMode::kMix), 0);
  EXPECT_EQ(static_cast<int>(LayerMode::kFade1), 1);
  EXPECT_EQ(static_cast<int>(LayerMode::kFade2), 2);
  EXPECT_EQ(static_cast<int>(LayerMode::kXfade), 3);
  EXPECT_EQ(static_cast<int>(LayerMode::kSwitch), 4);
  EXPECT_EQ(static_cast<int>(LayerMode::kSwitchMono), 5);
  EXPECT_EQ(static_cast<int>(LayerMode::kAlternate), 6);
  EXPECT_EQ(static_cast<int>(LayerMode::kHiHat), 7);
  EXPECT_EQ(kLayerModeCount, 8);
}

TEST(DynamicsCurve, EnumeratorsAreTheDeviceByteValues) {
  EXPECT_EQ(static_cast<int>(DynamicsCurve::kLinear), 0);
  EXPECT_EQ(static_cast<int>(DynamicsCurve::kLoud1), 1);
  EXPECT_EQ(static_cast<int>(DynamicsCurve::kLoud2), 2);
  EXPECT_EQ(static_cast<int>(DynamicsCurve::kLoud3), 3);
  EXPECT_EQ(kDynamicsCurveCount, 4);
}

// ---- Names: shared by the UI and written into .kit files ----

// Pinned as literals: a .kit file on disk holds these strings, so changing
// one silently stops the file it was written into from reading back.
TEST(LayerModeName, IsTheStableName) {
  EXPECT_EQ(LayerModeName(LayerMode::kMix), "MIX");
  EXPECT_EQ(LayerModeName(LayerMode::kFade1), "FADE1");
  EXPECT_EQ(LayerModeName(LayerMode::kFade2), "FADE2");
  EXPECT_EQ(LayerModeName(LayerMode::kXfade), "XFADE");
  EXPECT_EQ(LayerModeName(LayerMode::kSwitch), "SWITCH");
  EXPECT_EQ(LayerModeName(LayerMode::kSwitchMono), "SW(MONO)");
  EXPECT_EQ(LayerModeName(LayerMode::kAlternate), "ALTERNATE");
  EXPECT_EQ(LayerModeName(LayerMode::kHiHat), "HI-HAT");
}

TEST(LayerModeName, RoundTripsThroughParse) {
  for (const LayerMode mode : kAllModes) {
    EXPECT_EQ(ParseLayerMode(LayerModeName(mode), LayerMode::kHiHat), mode)
        << LayerModeName(mode);
  }
}

TEST(LayerModeName, ThrowsForAModeThatIsNotOne) {
  EXPECT_THROW((void)LayerModeName(static_cast<LayerMode>(kLayerModeCount)),
               std::out_of_range);
}

TEST(ParseLayerMode, FallsBackOnAnythingUnrecognised) {
  EXPECT_EQ(ParseLayerMode("NOPE", LayerMode::kXfade), LayerMode::kXfade);
  EXPECT_EQ(ParseLayerMode("", LayerMode::kXfade), LayerMode::kXfade);
  // Exact match only: the names are data, not user input.
  EXPECT_EQ(ParseLayerMode("mix", LayerMode::kXfade), LayerMode::kXfade);
  EXPECT_EQ(ParseLayerMode(" MIX", LayerMode::kXfade), LayerMode::kXfade);
}

TEST(DynamicsCurveName, IsTheStableName) {
  EXPECT_EQ(DynamicsCurveName(DynamicsCurve::kLinear), "LINEAR");
  EXPECT_EQ(DynamicsCurveName(DynamicsCurve::kLoud1), "LOUD1");
  EXPECT_EQ(DynamicsCurveName(DynamicsCurve::kLoud2), "LOUD2");
  EXPECT_EQ(DynamicsCurveName(DynamicsCurve::kLoud3), "LOUD3");
}

TEST(DynamicsCurveName, RoundTripsThroughParse) {
  for (const DynamicsCurve curve : kAllCurves) {
    EXPECT_EQ(
        ParseDynamicsCurve(DynamicsCurveName(curve), DynamicsCurve::kLoud3),
        curve)
        << DynamicsCurveName(curve);
  }
}

TEST(DynamicsCurveName, ThrowsForACurveThatIsNotOne) {
  EXPECT_THROW(
      (void)DynamicsCurveName(static_cast<DynamicsCurve>(kDynamicsCurveCount)),
      std::out_of_range);
}

TEST(ParseDynamicsCurve, FallsBackOnAnythingUnrecognised) {
  EXPECT_EQ(ParseDynamicsCurve("NOPE", DynamicsCurve::kLoud1),
            DynamicsCurve::kLoud1);
  EXPECT_EQ(ParseDynamicsCurve("", DynamicsCurve::kLoud1),
            DynamicsCurve::kLoud1);
  EXPECT_EQ(ParseDynamicsCurve("linear", DynamicsCurve::kLoud1),
            DynamicsCurve::kLoud1);
}

// ---- DynamicsGain: velocity -> loudness ----

// The curves differ in shape but not in reach: silence is silence and a full
// strike is full scale, whichever is selected.
TEST(DynamicsGain, EveryCurveAgreesAtBothEnds) {
  for (const DynamicsCurve curve : kAllCurves) {
    EXPECT_FLOAT_EQ(DynamicsGain(curve, 0), 0.0f) << DynamicsCurveName(curve);
    EXPECT_FLOAT_EQ(DynamicsGain(curve, 127), 1.0f) << DynamicsCurveName(curve);
  }
}

TEST(DynamicsGain, LinearIsTheStrikeAsAFractionOfFullScale) {
  EXPECT_FLOAT_EQ(DynamicsGain(DynamicsCurve::kLinear, 64), 64.0f / 127.0f);
  EXPECT_FLOAT_EQ(DynamicsGain(DynamicsCurve::kLinear, 1), 1.0f / 127.0f);
}

// The manual only promises LOUD1-3 make loud output "more readily produced"
// from softer strikes. Whatever the real shapes turn out to be, that ordering
// is the property worth holding: each curve sits above the previous one for
// every strike short of full.
TEST(DynamicsGain, TheLoudCurvesLiftSoftStrikesProgressively) {
  for (int velocity = 1; velocity < 127; ++velocity) {
    const float linear = DynamicsGain(DynamicsCurve::kLinear, velocity);
    const float loud1 = DynamicsGain(DynamicsCurve::kLoud1, velocity);
    const float loud2 = DynamicsGain(DynamicsCurve::kLoud2, velocity);
    const float loud3 = DynamicsGain(DynamicsCurve::kLoud3, velocity);

    EXPECT_GT(loud1, linear) << "velocity " << velocity;
    EXPECT_GT(loud2, loud1) << "velocity " << velocity;
    EXPECT_GT(loud3, loud2) << "velocity " << velocity;
  }
}

TEST(DynamicsGain, NeverFallsAsTheStrikeHardens) {
  for (const DynamicsCurve curve : kAllCurves) {
    for (int velocity = 0; velocity < 127; ++velocity) {
      EXPECT_LE(DynamicsGain(curve, velocity),
                DynamicsGain(curve, velocity + 1))
          << DynamicsCurveName(curve) << " at " << velocity;
    }
  }
}

TEST(DynamicsGain, ClampsAStrikeToTheMidiRange) {
  for (const DynamicsCurve curve : kAllCurves) {
    EXPECT_FLOAT_EQ(DynamicsGain(curve, 200), 1.0f);
    EXPECT_FLOAT_EQ(DynamicsGain(curve, -5), 0.0f);
  }
}

// ---- ComputeLayerWeights: the eight layer modes ----

TEST(ComputeLayerWeights, MixAlwaysSoundsBothLayers) {
  for (const int velocity : {1, 64, 127}) {
    const LayerWeights w = Weights(LayerMode::kMix, velocity);
    EXPECT_FLOAT_EQ(w.top, 1.0f);
    EXPECT_FLOAT_EQ(w.bottom, 1.0f);
  }
}

// Selection weights deliberately exclude loudness: the caller multiplies by
// DynamicsGain. A soft MIX hit still selects both layers fully.
TEST(ComputeLayerWeights, LeavesLoudnessOutOfTheWeights) {
  EXPECT_FLOAT_EQ(Weights(LayerMode::kMix, 1).top,
                  Weights(LayerMode::kMix, 127).top);
}

TEST(ComputeLayerWeights, Fade1AddsTheBottomLayerWholeAtTheFadePoint) {
  EXPECT_FLOAT_EQ(Weights(LayerMode::kFade1, kPoint - 1).bottom, 0.0f);
  EXPECT_FLOAT_EQ(Weights(LayerMode::kFade1, kPoint).bottom, 1.0f);
  EXPECT_FLOAT_EQ(Weights(LayerMode::kFade1, 127).bottom, 1.0f);

  // A is unconditional in FADE1; only B is gated.
  for (const int velocity : {1, kPoint, 127}) {
    EXPECT_FLOAT_EQ(Weights(LayerMode::kFade1, velocity).top, 1.0f);
  }
}

TEST(ComputeLayerWeights, Fade2BloomsTheBottomLayerInAcrossTheBand) {
  EXPECT_FLOAT_EQ(Weights(LayerMode::kFade2, kPoint - 1).bottom, 0.0f);
  EXPECT_FLOAT_EQ(Weights(LayerMode::kFade2, kPoint).bottom, 0.0f);
  EXPECT_FLOAT_EQ(Weights(LayerMode::kFade2, kMid).bottom, 0.5f);
  EXPECT_FLOAT_EQ(Weights(LayerMode::kFade2, kEnd).bottom, 1.0f);
  EXPECT_FLOAT_EQ(Weights(LayerMode::kFade2, 127).bottom, 1.0f);

  EXPECT_FLOAT_EQ(Weights(LayerMode::kFade2, kMid).top, 1.0f);
}

TEST(ComputeLayerWeights, XfadeTradesOneLayerForTheOther) {
  const LayerWeights at_point = Weights(LayerMode::kXfade, kPoint);
  EXPECT_FLOAT_EQ(at_point.top, 1.0f);
  EXPECT_FLOAT_EQ(at_point.bottom, 0.0f);

  const LayerWeights at_mid = Weights(LayerMode::kXfade, kMid);
  EXPECT_FLOAT_EQ(at_mid.top, 0.5f);
  EXPECT_FLOAT_EQ(at_mid.bottom, 0.5f);

  const LayerWeights at_end = Weights(LayerMode::kXfade, kEnd);
  EXPECT_FLOAT_EQ(at_end.top, 0.0f);
  EXPECT_FLOAT_EQ(at_end.bottom, 1.0f);
}

// What makes it a crossfade rather than two independent fades.
TEST(ComputeLayerWeights, XfadeKeepsTheTwoLayersSummingToOne) {
  for (int velocity = 1; velocity <= 127; ++velocity) {
    const LayerWeights w = Weights(LayerMode::kXfade, velocity);
    EXPECT_FLOAT_EQ(w.top + w.bottom, 1.0f) << "velocity " << velocity;
  }
}

TEST(ComputeLayerWeights, SwitchPicksOneLayerAtTheFadePoint) {
  const LayerWeights below = Weights(LayerMode::kSwitch, kPoint - 1);
  EXPECT_FLOAT_EQ(below.top, 1.0f);
  EXPECT_FLOAT_EQ(below.bottom, 0.0f);

  const LayerWeights at = Weights(LayerMode::kSwitch, kPoint);
  EXPECT_FLOAT_EQ(at.top, 0.0f);
  EXPECT_FLOAT_EQ(at.bottom, 1.0f);
}

TEST(ComputeLayerWeights, SwitchMonoSelectsLikeSwitch) {
  for (const int velocity : {kPoint - 1, kPoint, 127}) {
    const LayerWeights mono = Weights(LayerMode::kSwitchMono, velocity);
    const LayerWeights plain = Weights(LayerMode::kSwitch, velocity);
    EXPECT_FLOAT_EQ(mono.top, plain.top) << "velocity " << velocity;
    EXPECT_FLOAT_EQ(mono.bottom, plain.bottom) << "velocity " << velocity;
  }
}

// SW(MONO) is the only mode where a hit cuts off whatever is still ringing.
TEST(ComputeLayerWeights, OnlySwitchMonoChokes) {
  for (const LayerMode mode : kAllModes) {
    for (const int velocity : {1, kPoint, 127}) {
      EXPECT_EQ(Weights(mode, velocity).choke, mode == LayerMode::kSwitchMono)
          << LayerModeName(mode) << " at velocity " << velocity;
    }
  }
}

TEST(ComputeLayerWeights, AlternateFollowsTheFlipFlopNotTheStrike) {
  for (const int velocity : {1, 127}) {
    const LayerWeights first = ComputeLayerWeights(
        LayerMode::kAlternate, velocity, kPoint, kEnd, false, false);
    EXPECT_FLOAT_EQ(first.top, 1.0f);
    EXPECT_FLOAT_EQ(first.bottom, 0.0f);

    const LayerWeights second = ComputeLayerWeights(
        LayerMode::kAlternate, velocity, kPoint, kEnd, true, false);
    EXPECT_FLOAT_EQ(second.top, 0.0f);
    EXPECT_FLOAT_EQ(second.bottom, 1.0f);
  }
}

// Pedal down is the closed sound (the top layer), pedal up the open one.
TEST(ComputeLayerWeights, HiHatFollowsThePedalNotTheStrike) {
  for (const int velocity : {1, 127}) {
    const LayerWeights closed = ComputeLayerWeights(
        LayerMode::kHiHat, velocity, kPoint, kEnd, false, true);
    EXPECT_FLOAT_EQ(closed.top, 1.0f);
    EXPECT_FLOAT_EQ(closed.bottom, 0.0f);

    const LayerWeights open = ComputeLayerWeights(
        LayerMode::kHiHat, velocity, kPoint, kEnd, false, false);
    EXPECT_FLOAT_EQ(open.top, 0.0f);
    EXPECT_FLOAT_EQ(open.bottom, 1.0f);
  }
}

// The velocity-driven modes read the pad's own flip-flop and pedal state, but
// must not act on them.
TEST(ComputeLayerWeights, TheVelocityModesIgnoreTheFlipAndThePedal) {
  for (const LayerMode mode : {LayerMode::kMix,
                               LayerMode::kFade1,
                               LayerMode::kFade2,
                               LayerMode::kXfade,
                               LayerMode::kSwitch,
                               LayerMode::kSwitchMono}) {
    const LayerWeights plain =
        ComputeLayerWeights(mode, kMid, kPoint, kEnd, false, false);
    const LayerWeights noisy =
        ComputeLayerWeights(mode, kMid, kPoint, kEnd, true, true);
    EXPECT_FLOAT_EQ(plain.top, noisy.top) << LayerModeName(mode);
    EXPECT_FLOAT_EQ(plain.bottom, noisy.bottom) << LayerModeName(mode);
  }
}

// ---- A fade band with no room in it ----

// fade end is constrained >= fade point everywhere upstream, but a degenerate
// band must still behave: it collapses to a threshold at the fade point.
TEST(ComputeLayerWeights, AnEmptyFadeBandActsAsAThresholdAtTheFadePoint) {
  for (const int end : {kPoint, kPoint - 40}) {
    const LayerMode mode = LayerMode::kFade2;
    EXPECT_FLOAT_EQ(
        ComputeLayerWeights(mode, kPoint - 1, kPoint, end, false, false).bottom,
        0.0f)
        << "end " << end;
    EXPECT_FLOAT_EQ(
        ComputeLayerWeights(mode, kPoint, kPoint, end, false, false).bottom,
        1.0f)
        << "end " << end;
  }
}

TEST(ComputeLayerWeights, XfadeWithAnEmptyBandSwitchesOutright) {
  const LayerWeights below = ComputeLayerWeights(
      LayerMode::kXfade, kPoint - 1, kPoint, kPoint, false, false);
  EXPECT_FLOAT_EQ(below.top, 1.0f);
  EXPECT_FLOAT_EQ(below.bottom, 0.0f);

  const LayerWeights at = ComputeLayerWeights(
      LayerMode::kXfade, kPoint, kPoint, kPoint, false, false);
  EXPECT_FLOAT_EQ(at.top, 0.0f);
  EXPECT_FLOAT_EQ(at.bottom, 1.0f);
}

// ---- Which controls a mode reads ----

TEST(UsesFadePoint, IsTrueForTheModesThatReadIt) {
  EXPECT_TRUE(UsesFadePoint(LayerMode::kFade1));
  EXPECT_TRUE(UsesFadePoint(LayerMode::kFade2));
  EXPECT_TRUE(UsesFadePoint(LayerMode::kXfade));
  EXPECT_TRUE(UsesFadePoint(LayerMode::kSwitch));
  EXPECT_TRUE(UsesFadePoint(LayerMode::kSwitchMono));

  EXPECT_FALSE(UsesFadePoint(LayerMode::kMix));
  EXPECT_FALSE(UsesFadePoint(LayerMode::kAlternate));
  EXPECT_FALSE(UsesFadePoint(LayerMode::kHiHat));
}

TEST(UsesFadeEnd, IsTrueOnlyForTheBlendedModes) {
  for (const LayerMode mode : kAllModes) {
    const bool blended = mode == LayerMode::kFade2 || mode == LayerMode::kXfade;
    EXPECT_EQ(UsesFadeEnd(mode), blended) << LayerModeName(mode);
  }
}

// The fade end only means anything relative to the point, so a mode that
// reads the end must read the point too -- the UI shows them as a pair.
TEST(UsesFadeEnd, ImpliesUsesFadePoint) {
  for (const LayerMode mode : kAllModes) {
    if (UsesFadeEnd(mode)) {
      EXPECT_TRUE(UsesFadePoint(mode)) << LayerModeName(mode);
    }
  }
}

// ---- Defensive: a mode or curve that is not one ----

TEST(ComputeLayerWeights, SoundsNothingForAModeThatIsNotOne) {
  const LayerWeights w = Weights(static_cast<LayerMode>(kLayerModeCount), 127);
  EXPECT_FLOAT_EQ(w.top, 0.0f);
  EXPECT_FLOAT_EQ(w.bottom, 0.0f);
  EXPECT_FALSE(w.choke);
}

TEST(DynamicsGain, FallsBackToLinearForACurveThatIsNotOne) {
  EXPECT_FLOAT_EQ(
      DynamicsGain(static_cast<DynamicsCurve>(kDynamicsCurveCount), 64),
      64.0f / 127.0f);
}

}  // namespace
}  // namespace spdsx
