// The command strings behind Help > Install Command-Line Tool: a shell
// command that symlinks the bundled spdutil into /usr/local/bin, and the
// osascript wrapper that runs it with administrator privileges. Pure string
// logic, JUCE-free.
#ifndef SPDSX_PATCHEDIT_SOURCE_CLI_INSTALL_H_
#define SPDSX_PATCHEDIT_SOURCE_CLI_INSTALL_H_

#include <string>

namespace spdsx {

// Quotes s for POSIX sh: wraps it in single quotes, escaping embedded ones.
std::string ShellQuote(const std::string& s);

// The shell command that (re)creates link_dir/link_name -> source.
std::string InstallCliCommand(const std::string& source,
                              const std::string& link_dir,
                              const std::string& link_name);

// Wraps a shell command in an osascript expression that runs it with
// administrator privileges (the standard macOS password prompt).
std::string AdminInstallScript(const std::string& shell_command);

}  // namespace spdsx

#endif  // SPDSX_PATCHEDIT_SOURCE_CLI_INSTALL_H_
