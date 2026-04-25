/**
 * @file globals.h
 * @brief Shared global state, types, and declarations used across all modules.
 */
#pragma once

#include <string>
#include <vector>
#include <map>
#include <sys/types.h>

/**
 * @brief Tracks the readline history index of the last entry appended to the
 *        history file via `history -a`. -1 means no append has occurred yet.
 */
extern int last_appended_index;

/**
 * @brief Maps a command name to the path of its external completion script.
 *        Populated by `complete -C <script> <cmd>`.
 */
extern std::map<std::string, std::string> completion_registry;

/**
 * @brief Represents a single background job launched with `&`.
 */
struct BackgroundJob {
  int job_number;
  pid_t pid;
  std::string command;
  bool done = false;
};

/** @brief The live list of background jobs managed by this shell session. */
extern std::vector<BackgroundJob> bg_jobs;

/** @brief Null-terminated array of built-in command names. */
extern const char* builtin_commands[];
