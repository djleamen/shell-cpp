/**
 * @file globals.cpp
 * @brief Definitions of global variables shared across modules.
 */
#include "globals.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string_view>

int& last_appended_index() {
  static int val = -1;
  return val;
}

std::map<std::string, std::string, std::less<>>& completion_registry() {
  static std::map<std::string, std::string, std::less<>> val;
  return val;
}

std::vector<BackgroundJob>& bg_jobs() {
  static std::vector<BackgroundJob> val;
  return val;
}

std::map<std::string, std::string, std::less<>>& shell_variables() {
  static std::map<std::string, std::string, std::less<>> val;
  return val;
}

static size_t expandBraceVar(const std::string& arg, size_t i, std::string& out) {
  size_t start = i + 2;
  if (size_t close = arg.find('}', start); close != std::string::npos) {
    std::string varname = arg.substr(start, close - start);
    if (auto it = shell_variables().find(varname); it != shell_variables().end())
      out += it->second;
    return close + 1;
  }
  out += arg[i];
  return i + 1;
}

static size_t expandBareVar(const std::string& arg, size_t i, std::string& out) {
  size_t start = i + 1;
  size_t end   = start;
  while (end < arg.size() && (std::isalnum((unsigned char)arg[end]) || arg[end] == '_'))
    ++end;
  std::string varname = arg.substr(start, end - start);
  if (auto it = shell_variables().find(varname); it != shell_variables().end())
    out += it->second;
  return end;
}

void expandArgs(std::vector<std::string>& args) {
  for (auto& arg : args) {
    std::string expanded;
    size_t i = 0;
    while (i < arg.size()) {
      if (arg[i] == '$' && i + 1 < arg.size() && arg[i+1] == '{') {
        i = expandBraceVar(arg, i, expanded);
      } else if (arg[i] == '$' && i + 1 < arg.size() &&
                 (std::isalpha((unsigned char)arg[i+1]) || arg[i+1] == '_')) {
        i = expandBareVar(arg, i, expanded);
      } else {
        expanded += arg[i];
        ++i;
      }
    }
    arg = expanded;
  }
  if (args.size() > 1) {
    args.erase(
      std::remove_if(args.begin() + 1, args.end(),
                     [](std::string_view s) { return s.empty(); }),
      args.end()
    );
  }
}

const std::array<const char*, 10> builtin_commands = {
  "echo",
  "exit",
  "type",
  "pwd",
  "cd",
  "history",
  "jobs",
  "complete",
  "declare",
  nullptr
};
