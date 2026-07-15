#ifndef SPDSX_PATCHEDIT_TESTING_JUCE_TEST_ENVIRONMENT_H_
#define SPDSX_PATCHEDIT_TESTING_JUCE_TEST_ENVIRONMENT_H_

#include <memory>

#include <gtest/gtest.h>
#include <juce_events/juce_events.h>

namespace spdsx_testing {

// Brings up the JUCE message manager for the whole test session, so tests may
// construct Components or use juce::MessageManager without a real application.
// Registered once as a gtest global environment, in juce_test_environment.cc,
// so no individual suite has to set it up.
class JuceTestEnvironment : public ::testing::Environment {
public:
  void SetUp() override {
    initialiser_ = std::make_unique<juce::ScopedJuceInitialiser_GUI>();
  }

  void TearDown() override { initialiser_.reset(); }

private:
  std::unique_ptr<juce::ScopedJuceInitialiser_GUI> initialiser_;
};

}  // namespace spdsx_testing

#endif  // SPDSX_PATCHEDIT_TESTING_JUCE_TEST_ENVIRONMENT_H_
