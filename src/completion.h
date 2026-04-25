/**
 * @file completion.h
 * @brief GNU Readline tab-completion generators and hooks.
 */
#pragma once

#include "globals.h"

#include <vector>
#include <string>
#include <readline/readline.h>

/**
 * @brief Staging buffer populated by command_completion when an external
 *        completion script produces candidates.  Drained one entry per call
 *        by completer_generator.
 */
extern std::vector<std::string> completer_results;

/** @brief Readline generator for command-name tab completion. */
char* command_generator(const char* text, int state);

/** @brief Readline generator for filesystem path tab completion. */
char* filename_generator(const char* text, int state);

/** @brief Readline generator that iterates over completer_results. */
char* completer_generator(const char* text, int state);

/**
 * @brief Custom readline completion function registered via
 *        rl_attempted_completion_function.
 */
char** command_completion(const char* text, int start, int end);

/**
 * @brief Readline hook that prints completion matches on a single line and
 *        redraws the prompt.
 */
void display_matches_hook(char** matches, int num_matches, int max_length);
