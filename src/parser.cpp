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
  bool in_single = false;
  bool in_double = false;
  bool has_pipe = false;
  size_t i = 0;
  while (i < command.size()) {
    if (char c = command[i]; c == '\\' && !in_single && i + 1 < command.size()) {
      current += c;
      ++i;
      current += command[i];
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
    ++i;
  }
  if (!current.empty()) segments.push_back(current);
  return {has_pipe, segments};
}

static vector<string> tokenise(const string& cmd_str) {
  vector<string> tokens;
  string current;
  bool in_single = false;
  bool in_double = false;
  size_t i = 0;
  while (i < cmd_str.size()) {
    if (char c = cmd_str[i]; c == '\\' && !in_single && !in_double) {
      if (i + 1 < cmd_str.size()) { ++i; current += cmd_str[i]; }
      else                          current += c;
    } else if (c == '\\' && in_double) {
      char next = (i + 1 < cmd_str.size()) ? cmd_str[i + 1] : '\0';
      if (next == '"' || next == '\\') { ++i; current += cmd_str[i]; }
      else                               current += c;
    } else if (c == '\'' && !in_double) {
      in_single = !in_single;
    } else if (c == '"' && !in_single) {
      in_double = !in_double;
    } else if (isspace(static_cast<unsigned char>(c)) && !in_single && !in_double) {
      if (!current.empty()) { tokens.push_back(current); current.clear(); }
    } else {
      current += c;
    }
    ++i;
  }
  if (!current.empty()) tokens.push_back(current);
  return tokens;
}

static void extractRedirects(vector<string>& args, CommandInfo& info) {
  vector<string> clean;
  size_t i = 0;
  while (i < args.size()) {
    if (const string& tok = args[i]; (tok == ">>" || tok == "1>>") && i + 1 < args.size()) {
      info.has_redirect = true; info.is_append = true;
      ++i;
      info.output_file = args[i];
    } else if ((tok == ">" || tok == "1>") && i + 1 < args.size()) {
      info.has_redirect = true; info.is_append = false;
      ++i;
      info.output_file = args[i];
    } else if (tok == "2>>" && i + 1 < args.size()) {
      info.has_error_redirect = true; info.is_error_append = true;
      ++i;
      info.error_file = args[i];
    } else if (tok == "2>" && i + 1 < args.size()) {
      info.has_error_redirect = true; info.is_error_append = false;
      ++i;
      info.error_file = args[i];
    } else {
      clean.push_back(tok);
    }
    ++i;
  }
  args = move(clean);
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
