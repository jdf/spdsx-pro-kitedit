#include "cli_install.h"

#include <string>

namespace spdsx {

std::string ShellQuote(const std::string& s) {
  std::string out = "'";
  for (const char c : s) {
    if (c == '\'') {
      out += "'\\''";
    } else {
      out += c;
    }
  }
  out += "'";
  return out;
}

std::string InstallCliCommand(const std::string& source,
                              const std::string& link_dir,
                              const std::string& link_name) {
  return "mkdir -p " + ShellQuote(link_dir) + " && ln -sf " + ShellQuote(source)
      + " " + ShellQuote(link_dir + "/" + link_name);
}

std::string AdminInstallScript(const std::string& shell_command) {
  std::string escaped;
  for (const char c : shell_command) {
    if (c == '\\' || c == '"') {
      escaped += '\\';
    }
    escaped += c;
  }
  return "do shell script \"" + escaped + "\" with administrator privileges";
}

}  // namespace spdsx
