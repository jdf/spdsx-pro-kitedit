#include "cli_install.h"

#include <string>

#include <gtest/gtest.h>

namespace spdsx {
namespace {

TEST(ShellQuote, WrapsPlainTextInSingleQuotes) {
  EXPECT_EQ(ShellQuote("abc"), "'abc'");
}

TEST(ShellQuote, EscapesEmbeddedSingleQuotes) {
  EXPECT_EQ(ShellQuote("it's"), "'it'\\''s'");
}

TEST(InstallCliCommand, MakesTheDirThenForcesTheLink) {
  EXPECT_EQ(InstallCliCommand("/Applications/Foo.app/Contents/Helpers/spdutil",
                              "/usr/local/bin",
                              "spdutil"),
            "mkdir -p '/usr/local/bin' && ln -sf "
            "'/Applications/Foo.app/Contents/Helpers/spdutil' "
            "'/usr/local/bin/spdutil'");
}

TEST(InstallCliCommand, SurvivesASpaceyBundlePath) {
  const std::string cmd =
      InstallCliCommand("/Applications/My Apps/x.app/Contents/Helpers/spdutil",
                        "/usr/local/bin",
                        "spdutil");
  EXPECT_NE(cmd.find("'/Applications/My Apps/x.app/Contents/Helpers/spdutil'"),
            std::string::npos);
}

TEST(AdminInstallScript, WrapsTheCommandInDoShellScript) {
  EXPECT_EQ(AdminInstallScript("ln -sf 'a' 'b'"),
            "do shell script \"ln -sf 'a' 'b'\" with administrator privileges");
}

TEST(AdminInstallScript, EscapesBackslashesAndDoubleQuotes) {
  EXPECT_EQ(AdminInstallScript("echo \"x\\y\""),
            "do shell script \"echo \\\"x\\\\y\\\"\" "
            "with administrator privileges");
}

}  // namespace
}  // namespace spdsx
