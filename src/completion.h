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
std::vector<std::string>& getCompleterResults();

/**
 * @brief Readline generator for command-name tab completion.
 *
 * Enumerates built-in commands followed by executables found on PATH whose
 * names begin with @p text.  Conforms to the readline generator protocol:
 * called repeatedly until it returns nullptr.
 *
 * @param[in] text   Prefix typed so far.
 * @param[in] state  0 on the first call; non-zero on subsequent calls.
 * @return           Heap-allocated matching name (caller must free), or
 *                   nullptr when all matches are exhausted.
 */
char* command_generator(const char* text, int state);

/**
 * @brief Readline generator for filesystem path tab completion.
 *
 * Enumerates files and directories whose names begin with the basename of
 * @p text, prefixed by the directory part of @p text.  Directories are
 * returned with a trailing '/'.  Conforms to the readline generator protocol.
 *
 * @param[in] text   Path prefix typed so far.
 * @param[in] state  0 on the first call; non-zero on subsequent calls.
 * @return           Heap-allocated matching path (caller must free), or
 *                   nullptr when all matches are exhausted.
 */
char* filename_generator(const char* text, int state);

/**
 * @brief Readline generator that drains completer_results one entry at a time.
 *
 * Populated by command_completion when an external completion script produces
 * candidates.  Conforms to the readline generator protocol.
 *
 * @param[in] text   Completion prefix (unused; results are pre-filtered).
 * @param[in] state  0 on the first call; non-zero on subsequent calls.
 * @return           Heap-allocated candidate string (caller must free), or
 *                   nullptr when completer_results is exhausted.
 */
char* completer_generator(const char* text, int state);

/**
 * @brief Custom completion function registered as rl_attempted_completion_function.
 *
 * - When @p start == 0 (completing the command word), delegates to command_generator.
 * - Otherwise looks up the command word in completion_registry; if a script is
 *   registered, invokes it with COMP_LINE/COMP_POINT and uses completer_generator
 *   to return its output.
 * - Falls back to filename_generator for unregistered commands.
 *
 * @param[in] text   Word being completed.
 * @param[in] start  Byte offset of @p text in rl_line_buffer.
 * @param[in] end    End offset of @p text in rl_line_buffer (unused).
 * @return           Heap-allocated array of match strings for readline, or
 *                   nullptr to invoke the default completer.
 */
char** command_completion(const char* text, int start, int end);

/**
 * @brief Readline hook that displays completion matches on a single line and
 *        redraws the prompt.  Registered via rl_completion_display_matches_hook.
 *
 * Prints all matches space-separated on one line instead of readline's default
 * columnar layout, then redraws the current input line.
 *
 * @param[in] matches      Null-terminated array; matches[0] is the common prefix,
 *                         candidates begin at matches[1].
 * @param[in] num_matches  Count of candidate strings (excluding the prefix).
 * @param[in] max_length   Length of the longest match (unused).
 */
void display_matches_hook(char** matches, int num_matches, int max_length);
