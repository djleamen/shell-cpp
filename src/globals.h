/**
 * @file globals.h
 * @brief Shared global state, types, and declarations used across all modules.
 */
#pragma once

#include <array>
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
extern std::map<std::string, std::string, std::less<>> completion_registry;

/**
 * @brief Represents a single background job launched with `&`.
 *
 * @var BackgroundJob::job_number  Shell-assigned job index (≥ 1), unique among active jobs.
 * @var BackgroundJob::pid         OS process ID of the background child.
 * @var BackgroundJob::command     Raw command string as typed by the user.
 * @var BackgroundJob::done        Set to true when the child has exited or been signalled.
 */
struct BackgroundJob {
  int job_number;
  pid_t pid;
  std::string command;
  bool done = false;
};

/** @brief The live list of background jobs managed by this shell session. */
extern std::vector<BackgroundJob> bg_jobs;

/** @brief Shell variable store populated by the declare builtin. */
extern std::map<std::string, std::string, std::less<>> shell_variables;

/** @brief Null-terminated array of built-in command names. */
extern const std::array<const char*, 10> builtin_commands;

/**
 * @brief Expands $VAR and ${VAR} references in-place for every element of
 *        @p args.  Non-program arguments that expand to an empty string
 *        (i.e. an unset variable with no surrounding text) are dropped.
 *        args[0] (the program name) is never removed.
 *
 * @param[in,out] args  Token list to expand; modified in place.
 */
void expandArgs(std::vector<std::string>& args);
