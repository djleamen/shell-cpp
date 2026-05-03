/**
 * @file globals.cpp
 * @brief Definitions of global variables shared across modules.
 */
#include "globals.h"

#include <algorithm>
#include <cctype>

int last_appended_index = -1;

std::map<std::string, std::string> completion_registry;

std::vector<BackgroundJob> bg_jobs;

std::map<std::string, std::string> shell_variables;

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
          auto it = shell_variables.find(varname);
          if (it != shell_variables.end()) expanded += it->second;
          i = close + 1;
        } else {
          expanded += arg[i++];
        }
      } else if (arg[i] == '$' && i + 1 < arg.size() &&
                 (std::isalpha((unsigned char)arg[i+1]) || arg[i+1] == '_')) {
        size_t start = i + 1, end = start;
        while (end < arg.size() && (std::isalnum((unsigned char)arg[end]) || arg[end] == '_'))
          ++end;
        std::string varname = arg.substr(start, end - start);
        auto it = shell_variables.find(varname);
        if (it != shell_variables.end()) expanded += it->second;
        i = end;
      } else {
        expanded += arg[i++];
      }
    }
    arg = expanded;
  }
  if (args.size() > 1) {
    args.erase(
      std::remove_if(args.begin() + 1, args.end(),
                     [](const std::string& s) { return s.empty(); }),
      args.end()
    );
  }
}

const char* builtin_commands[] = {
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
