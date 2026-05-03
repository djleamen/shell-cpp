/**
 * @file completion.cpp
 * @brief GNU Readline tab-completion generators and hooks.
 */
#include "completion.h"
#include "executor.h"

#include <sstream>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <algorithm>
#include <filesystem>
#include <readline/history.h>

using namespace std;
namespace fs = std::filesystem;

vector<string>& getCompleterResults() {
  static vector<string> results;
  return results;
}

static bool isExecutable(const fs::directory_entry& entry) {
  using enum fs::perms;
  auto perms = entry.status().permissions();
  return (perms & owner_exec) != none ||
         (perms & group_exec) != none ||
         (perms & others_exec) != none;
}

static vector<string> collectPathExecutables(string_view prefix) {
  vector<string> results;
  const char* path_env = getenv("PATH");
  if (!path_env) return results;

  stringstream ss(path_env);
  string dir_str;
  while (getline(ss, dir_str, ':')) {
    fs::path dir(dir_str);
    if (!fs::exists(dir)) continue;
    try {
      for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        string filename = entry.path().filename().string();
        if (!filename.starts_with(prefix)) continue;
        if (!isExecutable(entry)) continue;
        if (ranges::find(results, filename) == results.end()) {
          results.push_back(filename);
        }
      }
    } catch (const fs::filesystem_error&) {
      // Skip directories that cannot be read (permission denied, broken symlinks, etc.)
    }
  }
  return results;
}

char* command_generator(const char* text, int state) {
  static int list_index;
  static string search_text;
  static vector<string> path_executables;
  static size_t path_exec_index;
  static bool builtins_done;

  if (!state) {
    search_text = text ? text : "";
    list_index = 0;
    path_exec_index = 0;
    builtins_done = false;
    path_executables = collectPathExecutables(search_text);
  }

  if (!builtins_done) {
    while (builtin_commands[list_index]) {
      const char* name = builtin_commands[list_index++];
      string cmd_name(name);
      if (cmd_name.starts_with(search_text)) {
        return strdup(name);
      }
    }
    builtins_done = true;
  }

  if (path_exec_index < path_executables.size()) {
    const string& exe = path_executables[path_exec_index];
    ++path_exec_index;
    return strdup(exe.c_str());
  }

  return nullptr;
}

char* filename_generator(const char* text, int state) {
  static vector<string> matches;
  static size_t match_index;

  if (!state) {
    matches.clear();
    match_index = 0;

    string input(text ? text : "");
    string dir_path;
    string file_prefix;
    if (size_t last_slash = input.rfind('/'); last_slash != string::npos) {
      dir_path = input.substr(0, last_slash + 1);
      file_prefix = input.substr(last_slash + 1);
    } else {
      dir_path = "";
      file_prefix = input;
    }

    string search_dir = dir_path.empty() ? "." : dir_path;

    try {
      for (const auto& entry : fs::directory_iterator(search_dir)) {
        string filename = entry.path().filename().string();
        if (!filename.starts_with(file_prefix)) continue;
        string match = dir_path + filename;
        if (fs::is_directory(entry.status())) {
          match += "/";
        }
        matches.push_back(match);
      }
    } catch (const fs::filesystem_error&) {
      // Skip directories that cannot be iterated (permission denied, etc.)
    }
  }

  if (match_index < matches.size()) {
    const string& match = matches[match_index];
    ++match_index;
    if (!match.empty() && match.back() == '/') {
      rl_completion_append_character = '\0';
    } else {
      rl_completion_append_character = ' ';
    }
    return strdup(match.c_str());
  }

  return nullptr;
}

char* completer_generator(const char* /*text*/, int state) {
  static size_t idx;
  if (!state) {
    idx = 0;
  }
  while (idx < getCompleterResults().size()) {
    const string& candidate = getCompleterResults()[idx];
    ++idx;
    if (candidate.empty()) continue;
    rl_completion_append_character = ' ';
    return strdup(candidate.c_str());
  }
  return nullptr;
}

char** command_completion(const char* text, int start, int /*end*/) {
  if (start == 0) {
    return rl_completion_matches(text, command_generator);
  }

  string line(rl_line_buffer ? rl_line_buffer : "");
  string cmd;
  {
    size_t sp = line.find(' ');
    cmd = (sp != string::npos) ? line.substr(0, sp) : line;
  }

  auto it = completion_registry.find(cmd);
  if (it != completion_registry.end()) {
    getCompleterResults().clear();

    string before_cursor = line.substr(0, start);
    string prev_word;
    {
      vector<string> tokens;
      istringstream iss(before_cursor);
      string tok;
      while (iss >> tok) tokens.push_back(tok);
      if (!before_cursor.empty() && !isspace((unsigned char)before_cursor.back())) {
        if (tokens.size() >= 2) prev_word = tokens[tokens.size() - 2];
      } else {
        if (tokens.size() >= 1) prev_word = tokens.back();
      }
    }

    auto shell_escape = [](const string& s) -> string {
      string out = "'";
      for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
      }
      out += "'";
      return out;
    };

    string comp_line = line.substr(0, start) + string(text);
    string comp_point = to_string(comp_line.size());

    string invoke = "COMP_LINE=" + shell_escape(comp_line) +
                    " COMP_POINT=" + comp_point +
                    " " + it->second + " " + shell_escape(cmd) + " " +
                    shell_escape(string(text)) + " " + shell_escape(prev_word);

    FILE* fp = popen(invoke.c_str(), "r");
    if (fp) {
      char buf[4096];
      while (fgets(buf, sizeof(buf), fp)) {
        string out(buf);
        if (!out.empty() && out.back() == '\n') out.pop_back();
        if (!out.empty()) getCompleterResults().push_back(out);
      }
      pclose(fp);
    }
    if (!getCompleterResults().empty()) {
      rl_attempted_completion_over = 1;
      return rl_completion_matches(text, completer_generator);
    }
  }

  rl_attempted_completion_over = 1;
  return rl_completion_matches(text, filename_generator);
}

void display_matches_hook(char** matches, int num_matches, int /*max_length*/) {
  fprintf(rl_outstream, "\n");
  for (int i = 1; i <= num_matches; i++) {
    if (i > 1) fprintf(rl_outstream, "  ");
    fprintf(rl_outstream, "%s", matches[i]);
  }
  fprintf(rl_outstream, "\n");
  rl_on_new_line();
  rl_redisplay();
}
