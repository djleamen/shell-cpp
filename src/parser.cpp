/**
 * @file parser.cpp
 * @brief Implementation of parsePipeline().
 */
#include "parser.h"

#include <cctype>

using namespace std;

static pair<bool, vector<string>> splitByPipe(const string& command) {
  vector<string> segments;
  string current;
  bool in_single = false, in_double = false, has_pipe = false;

  for (size_t i = 0; i < command.size(); ++i) {
    char c = command[i];
    if (c == '\\' && !in_single && i + 1 < command.size()) {
      current += c;
      current += command[++i];
    } else if (c == '\'' && !in_double) {
      in_single = !in_single;
      current += c;
    } else if (c == '"' && !in_single) {
      in_double = !in_double;
      current += c;
    } else if (c == '|' && !in_single && !in_double) {
      if (!current.empty()) segments.push_back(current);
      current.clear();
      has_pipe = true;
    } else {
      current += c;
    }
  }
  if (!current.empty()) segments.push_back(current);
  return {has_pipe, segments};
}

static vector<string> tokenise(const string& cmd_str) {
  vector<string> tokens;
  string current;
  bool in_single = false, in_double = false;

  for (size_t i = 0; i < cmd_str.size(); ++i) {
    char c = cmd_str[i];
    if (c == '\\' && !in_single && !in_double) {
      if (i + 1 < cmd_str.size()) current += cmd_str[++i];
      else                         current += c;
    } else if (c == '\\' && in_double) {
      if (i + 1 < cmd_str.size()) {
        char next = cmd_str[i + 1];
        if (next == '"' || next == '\\') { current += next; ++i; }
        else                              current += c;
      } else {
        current += c;
      }
    } else if (c == '\'' && !in_double) {
      in_single = !in_single;
    } else if (c == '"' && !in_single) {
      in_double = !in_double;
    } else if (isspace(static_cast<unsigned char>(c)) && !in_single && !in_double) {
      if (!current.empty()) { tokens.push_back(current); current.clear(); }
    } else {
      current += c;
    }
  }
  if (!current.empty()) tokens.push_back(current);
  return tokens;
}

static void extractRedirects(vector<string>& args, CommandInfo& info) {
  for (size_t i = 0; i + 1 < args.size(); ++i) {
    const string& tok = args[i];
    if (tok == ">>" || tok == "1>>") {
      info.has_redirect = true; info.is_append = true;
      info.output_file  = args[i + 1];
      args.erase(args.begin() + i, args.begin() + i + 2); --i;
    } else if (tok == ">" || tok == "1>") {
      info.has_redirect = true; info.is_append = false;
      info.output_file  = args[i + 1];
      args.erase(args.begin() + i, args.begin() + i + 2); --i;
    } else if (tok == "2>>") {
      info.has_error_redirect = true; info.is_error_append = true;
      info.error_file = args[i + 1];
      args.erase(args.begin() + i, args.begin() + i + 2); --i;
    } else if (tok == "2>") {
      info.has_error_redirect = true; info.is_error_append = false;
      info.error_file = args[i + 1];
      args.erase(args.begin() + i, args.begin() + i + 2); --i;
    }
  }
}

static CommandInfo parseCommand(const string& cmd_str) {
  CommandInfo info{};
  vector<string> args = tokenise(cmd_str);
  extractRedirects(args, info);
  info.args = move(args);
  return info;
}

PipelineInfo parsePipeline(const string& command) {
  auto [has_pipe, segments] = splitByPipe(command);
  PipelineInfo pipeline;
  pipeline.has_pipe = has_pipe;
  for (const auto& seg : segments)
    pipeline.commands.push_back(parseCommand(seg));
  return pipeline;
}
