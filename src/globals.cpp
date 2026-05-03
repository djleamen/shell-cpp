/**
 * @file globals.cpp
 * @brief Definitions of global variables shared across modules.
 */
#include "globals.h"

int last_appended_index = -1;

std::map<std::string, std::string> completion_registry;

std::vector<BackgroundJob> bg_jobs;

std::map<std::string, std::string> shell_variables;

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
