#include "juce_test_environment.h"

#include <gtest/gtest.h>

namespace {

// Stand up the JUCE message manager for the whole test session; JUCE-backed
// code under test expects it. Registered here, in a translation unit the test
// binary always links, so no suite has to remember to do it. gtest owns and
// frees the environment.
const ::testing::Environment* const kJuceEnv =
    ::testing::AddGlobalTestEnvironment(new spdsx_testing::JuceTestEnvironment);

}  // namespace
