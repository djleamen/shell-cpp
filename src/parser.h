/**
 * @file parser.h
 * @brief Command-line parsing: tokenization, quoting, and pipeline splitting.
 */
#pragma once

#include <string>
#include <vector>

/**
 * @brief Parsed representation of a single command within a pipeline.
 *
 * @var args              Tokenized command name and arguments (quoting and
 *                         escaping already resolved).
 * @var output_file       Target path for stdout redirection; empty if none.
 * @var has_redirect      True when a `>` or `>>` operator was present.
 * @var is_append         True when `>>` (append) was used instead of `>`.
 * @var error_file        Target path for stderr redirection; empty if none.
 * @var has_error_redirect True when a `2>` or `2>>` operator was present.
 * @var is_error_append   True when `2>>` (append) was used instead of `2>`.
 */
struct CommandInfo {
  std::vector<std::string> args;
  std::string output_file;
  bool has_redirect;
  bool is_append;
  std::string error_file;
  bool has_error_redirect;
  bool is_error_append;
};

/**
 * @brief Parsed representation of a complete input line, potentially
 *        containing multiple commands connected by `|`.
 *
 * @var commands   Ordered list of CommandInfo objects, one per pipe-separated segment.
 * @var has_pipe   True when at least one `|` operator separated the commands.
 */
struct PipelineInfo {
  std::vector<CommandInfo> commands;
  bool has_pipe;
};

/**
 * @brief Parses a raw command string into a PipelineInfo structure.
 *
 * @param[in] command  The raw command line as entered by the user.
 * @return A fully populated PipelineInfo ready for execution.
 */
PipelineInfo parsePipeline(const std::string& command);
