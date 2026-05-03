/**
 * @file globals.cpp
 * @brief Definitions of global variables shared across modules.
 */
#include "globals.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string_view>

int last_appended_index = -1;

std::map<std::string, std::string, std::less<>> completion_registry;

std::vector<BackgroundJob> bg_jobs;

std::map<std::string, std::string, std::less<>> shell_variables;

void expandArgs(std::vector<std::string>& args) {
  for (auto& arg : args) {
    std::string expanded;
    size_t i = 0;
    while (i < arg.size()) {
      if (arg[i] == '$' && i + 1 < arg.size() && arg[i+1] == '{') {
        size_t start = i + 2;
        size_t close = arg.find('}', start);
        if (close != std::string::npos) {
          std::string varname = arg.substr(start, close - start);
          if (auto it = shell_variables.find(varname); it != shell_variables.end()) expanded += it->second;
          i = close + 1;
        } else {
          expanded += arg[i];
          ++i;
        }
      } else if (arg[i] == '$' && i + 1 < arg.size() &&
                 (std::isalpha((unsigned char)arg[i+1]) || arg[i+1] == '_')) {
        size_t start = i + 1;
        size_t end = start;
        while (end < arg.size() && (std::isalnum((unsigned char)arg[end]) || arg[end] == '_'))
          ++end;
        std::string varname = arg.substr(start, end - start);
        if (auto it = shell_variables.find(varname); it != shell_variables.end()) expanded += it->second;
        i = end;
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
