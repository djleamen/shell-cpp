/**
 * @file parser.cpp
 * @brief Implementation of parsePipeline().
 */
#include "parser.h"

#include <sstream>
#include <cctype>

using namespace std;

PipelineInfo parsePipeline(const string& command) {
  PipelineInfo pipeline;
  pipeline.has_pipe = false;

  vector<string> command_strings;
  string current_cmd;
  bool in_single_quotes = false;
  bool in_double_quotes = false;

  for (size_t i = 0; i < command.length(); ++i) {
    char c = command[i];

    if (c == '\\' && !in_single_quotes && i + 1 < command.length()) {
      current_cmd += c;
      current_cmd += command[++i];
    }
    else if (c == '\'' && !in_double_quotes) {
      in_single_quotes = !in_single_quotes;
      current_cmd += c;
    }
    else if (c == '"' && !in_single_quotes) {
      in_double_quotes = !in_double_quotes;
      current_cmd += c;
    }
    else if (c == '|' && !in_single_quotes && !in_double_quotes) {
      if (!current_cmd.empty()) {
        command_strings.push_back(current_cmd);
        current_cmd.clear();
      }
      pipeline.has_pipe = true;
    }
    else {
      current_cmd += c;
    }
  }
  if (!current_cmd.empty()) {
    command_strings.push_back(current_cmd);
  }

  for (const auto& cmd_str : command_strings) {
    CommandInfo info;
    info.has_redirect = false;
    info.is_append = false;
    info.has_error_redirect = false;
    info.is_error_append = false;

    vector<string> args;
    string current_arg;
    in_single_quotes = false;
    in_double_quotes = false;

    for (size_t i = 0; i < cmd_str.length(); ++i) {
      char c = cmd_str[i];

      if (c == '\\' && !in_single_quotes && !in_double_quotes) {
        if (i + 1 < cmd_str.length()) {
          ++i;
          current_arg += cmd_str[i];
        }
        else {
          current_arg += c;
        }
      }
      else if (c == '\\' && in_double_quotes && !in_single_quotes) {
        if (i + 1 < cmd_str.length()) {
          char next = cmd_str[i + 1];
          if (next == '"' || next == '\\') {
            ++i;
            current_arg += next;
          } else {
            current_arg += c;
          }
        } else {
          current_arg += c;
        }
      }
      else if (c == '\'' && !in_double_quotes) {
        in_single_quotes = !in_single_quotes;
      } else if (c == '"' && !in_single_quotes) {
        in_double_quotes = !in_double_quotes;
      } else if (isspace(c) && !in_single_quotes && !in_double_quotes) {
        if (!current_arg.empty()) {
          args.push_back(current_arg);
          current_arg.clear();
        }
      } else {
        current_arg += c;
      }
    }
    if (!current_arg.empty()) {
      args.push_back(current_arg);
    }

    for (size_t i = 0; i < args.size(); ++i) {
      if (args[i] == ">>" || args[i] == "1>>") {
        info.has_redirect = true;
        info.is_append = true;
        if (i + 1 < args.size()) {
          info.output_file = args[i + 1];
          args.erase(args.begin() + i, args.begin() + i + 2);
          --i;
        }
      } else if (args[i] == ">" || args[i] == "1>") {
        info.has_redirect = true;
        info.is_append = false;
        if (i + 1 < args.size()) {
          info.output_file = args[i + 1];
          args.erase(args.begin() + i, args.begin() + i + 2);
          --i;
        }
      } else if (args[i] == "2>>") {
        info.has_error_redirect = true;
        info.is_error_append = true;
        if (i + 1 < args.size()) {
          info.error_file = args[i + 1];
          args.erase(args.begin() + i, args.begin() + i + 2);
          --i;
        }
      } else if (args[i] == "2>") {
        info.has_error_redirect = true;
        info.is_error_append = false;
        if (i + 1 < args.size()) {
          info.error_file = args[i + 1];
          args.erase(args.begin() + i, args.begin() + i + 2);
          --i;
        }
      }
    }

    info.args = args;
    pipeline.commands.push_back(info);
  }

  return pipeline;
}
