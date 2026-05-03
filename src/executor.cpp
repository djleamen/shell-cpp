/**
 * @file executor.cpp
 * @brief Implementations of command lookup, built-in execution, and
 *        program/pipeline running.
 */
#include "executor.h"

#include <iostream>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <string_view>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <cerrno>
#include <readline/history.h>

using namespace std;
namespace fs = std::filesystem;

bool isBuiltin(string_view cmd) {
  return cmd == "exit" || cmd == "echo" || cmd == "type" || cmd == "pwd"
      || cmd == "cd"   || cmd == "history" || cmd == "jobs" || cmd == "complete"
      || cmd == "declare";
}

string findInPath(string_view program) {
  const char* path_env = getenv("PATH");
  if (!path_env) return "";
  stringstream ss(path_env);
  string dir;
  while (getline(ss, dir, ':')) {
    fs::path full_path = fs::path(dir) / program;
    if (fs::exists(full_path) &&
        (fs::status(full_path).permissions() & fs::perms::owner_exec) != fs::perms::none) {
      return full_path.string();
    }
  }
  return "";
}

static void builtinEcho(const vector<string>& args) {
  for (size_t i = 1; i < args.size(); ++i) {
    if (i > 1) cout << " ";
    cout << args[i];
  }
  cout << endl;
}

static void builtinType(const vector<string>& args) {
  if (args.size() <= 1) return;
  const string& arg = args[1];
  if (isBuiltin(arg)) {
    cout << arg << " is a shell builtin" << endl;
  } else if (string path = findInPath(arg); !path.empty()) {
    cout << arg << " is " << path << endl;
  } else {
    cout << arg << ": not found" << endl;
  }
}

static void builtinPwd() {
  try {
    cout << fs::current_path().string() << endl;
  } catch (const fs::filesystem_error&) {
    cerr << "pwd: error getting current directory" << endl;
  }
}

static void builtinCd(const vector<string>& args) {
  if (args.size() <= 1) return;
  string path = args[1];
  if (path == "~" || path.starts_with("~/")) {
    const char* home_env = getenv("HOME");
    if (home_env) {
      string home(home_env);
      path = (path == "~") ? home : home + path.substr(1);
    }
  }
  if (chdir(path.c_str()) != 0) {
    cout << "cd: " << path << ": No such file or directory" << endl;
  }
}

static void historyRead(const string& filename) {
  ifstream file(filename);
  if (!file.is_open()) {
    cerr << "history: " << filename << ": No such file or directory" << endl;
    return;
  }
  string line;
  while (getline(file, line)) {
    if (!line.empty()) add_history(line.c_str());
  }
}

static void historyWrite(const string& filename) {
  ofstream file(filename);
  if (!file.is_open()) {
    cerr << "history: " << filename << ": cannot create" << endl;
    return;
  }
  int end = history_base + history_length;
  for (int i = history_base; i < end; ++i) {
    if (const HIST_ENTRY* entry = history_get(i)) file << entry->line << endl;
  }
}

static void historyAppend(const string& filename) {
  ofstream file(filename, ios::app);
  if (!file.is_open()) {
    cerr << "history: " << filename << ": cannot create" << endl;
    return;
  }
  int start = (last_appended_index == -1) ? history_base : last_appended_index + 1;
  int end = history_base + history_length;
  for (int i = start; i < end; ++i) {
    if (const HIST_ENTRY* entry = history_get(i)) file << entry->line << endl;
  }
  last_appended_index = end - 1;
}

static void historyList(const vector<string>& args) {
  int start = history_base;
  int end = history_base + history_length;
  if (args.size() > 1 && args[1] != "-r" && args[1] != "-w") {
    int n = stoi(args[1]);
    start = max(history_base, end - n);
  }
  for (int i = start; i < end; ++i) {
    if (const HIST_ENTRY* entry = history_get(i)) cout << "    " << i << "  " << entry->line << endl;
  }
}

static void builtinHistory(const vector<string>& args) {
  if (args.size() > 2 && args[1] == "-r")     { historyRead(args[2]); }
  else if (args.size() > 2 && args[1] == "-w") { historyWrite(args[2]); }
  else if (args.size() > 2 && args[1] == "-a") { historyAppend(args[2]); }
  else                                          { historyList(args); }
}

void executeBuiltinInChild(const vector<string>& args) {
  const string& program = args[0];
  if (program == "exit")         { exit(0); }
  else if (program == "echo")    { builtinEcho(args); }
  else if (program == "type")    { builtinType(args); }
  else if (program == "pwd")     { builtinPwd(); }
  else if (program == "cd")      { builtinCd(args); }
  else if (program == "history") { builtinHistory(args); }
  exit(0);
}

static vector<char*> buildArgv(const vector<string>& args, vector<vector<char>>& storage) {
  storage.reserve(args.size());
  vector<char*> argv;
  argv.reserve(args.size() + 1);
  for (const auto& arg : args) {
    storage.emplace_back(arg.begin(), arg.end());
    storage.back().push_back('\0');
    argv.push_back(storage.back().data());
  }
  argv.push_back(nullptr);
  return argv;
}

void executeProgram(const string& path, const vector<string>& args,
                    const string& output_file, bool is_append,
                    const string& error_file, bool is_error_append) {
  pid_t pid = fork();
  if (pid == 0) {
    if (!output_file.empty()) {
      int flags = O_WRONLY | O_CREAT | (is_append ? O_APPEND : O_TRUNC);
      int fd = open(output_file.c_str(), flags, 0666);
      if (fd == -1) { cerr << "Failed to open " << output_file << " for writing" << endl; exit(1); }
      dup2(fd, STDOUT_FILENO);
      close(fd);
    }
    if (!error_file.empty()) {
      int flags = O_WRONLY | O_CREAT | (is_error_append ? O_APPEND : O_TRUNC);
      int fd = open(error_file.c_str(), flags, 0666);
      if (fd == -1) { cerr << "Failed to open " << error_file << " for writing" << endl; exit(1); }
      dup2(fd, STDERR_FILENO);
      close(fd);
    }
    vector<vector<char>> argv_storage;
    auto argv = buildArgv(args, argv_storage);
    execv(path.c_str(), argv.data());
    cerr << "Failed to execute " << path << endl;
    exit(1);
  } else if (pid > 0) {
    int status;
    waitpid(pid, &status, 0);
  } else {
    cerr << "Fork failed" << endl;
  }
}

void executePipeline(const vector<CommandInfo>& commands) {
  if (commands.empty()) return;

  int num_commands = commands.size();
  vector<pid_t> pids;

  vector<vector<int>> pipes(num_commands - 1, vector<int>(2));
  for (int i = 0; i < num_commands - 1; ++i) {
    if (pipe(pipes[i].data()) == -1) {
      cerr << "Pipe creation failed" << endl;
      return;
    }
  }

  for (int i = 0; i < num_commands; ++i) {
    const CommandInfo& cmd = commands[i];
    bool builtin = isBuiltin(cmd.args[0]);
    string path;
    if (!builtin) {
      path = findInPath(cmd.args[0]);
      if (path.empty()) {
        cerr << cmd.args[0] << ": command not found" << endl;
        for (int j = 0; j < num_commands - 1; ++j) {
          close(pipes[j][0]);
          close(pipes[j][1]);
        }
        return;
      }
    }

    pid_t pid = fork();
    if (pid == 0) {
      if (i > 0) dup2(pipes[i - 1][0], STDIN_FILENO);
      if (i < num_commands - 1) dup2(pipes[i][1], STDOUT_FILENO);

      for (int j = 0; j < num_commands - 1; ++j) {
        close(pipes[j][0]);
        close(pipes[j][1]);
      }

      if (i == num_commands - 1 && cmd.has_redirect && !cmd.output_file.empty()) {
        int flags = O_WRONLY | O_CREAT | (cmd.is_append ? O_APPEND : O_TRUNC);
        int fd = open(cmd.output_file.c_str(), flags, 0666);
        if (fd == -1) { cerr << "Failed to open " << cmd.output_file << " for writing" << endl; exit(1); }
        dup2(fd, STDOUT_FILENO);
        close(fd);
      }

      if (cmd.has_error_redirect && !cmd.error_file.empty()) {
        int flags = O_WRONLY | O_CREAT | (cmd.is_error_append ? O_APPEND : O_TRUNC);
        int fd = open(cmd.error_file.c_str(), flags, 0666);
        if (fd == -1) { cerr << "Failed to open " << cmd.error_file << " for writing" << endl; exit(1); }
        dup2(fd, STDERR_FILENO);
        close(fd);
      }

      if (builtin) {
        executeBuiltinInChild(cmd.args);
      } else {
        vector<vector<char>> argv_storage;
        auto argv = buildArgv(cmd.args, argv_storage);
        execv(path.c_str(), argv.data());
        cerr << "Failed to execute " << path << endl;
        exit(1);
      }
    } else if (pid > 0) {
      pids.push_back(pid);
    } else {
      cerr << "Fork failed" << endl;
    }
  }

  for (int i = 0; i < num_commands - 1; ++i) {
    close(pipes[i][0]);
    close(pipes[i][1]);
  }

  for (pid_t pid : pids) {
    int status;
    waitpid(pid, &status, 0);
  }
}

void setupBuiltinRedirects(const CommandInfo& cmd,
                           int& saved_stdout, int& redirect_fd,
                           int& saved_stderr, int& error_redirect_fd) {
  if (cmd.has_redirect && !cmd.output_file.empty()) {
    saved_stdout = dup(STDOUT_FILENO);
    int flags = O_WRONLY | O_CREAT | (cmd.is_append ? O_APPEND : O_TRUNC);
    redirect_fd = open(cmd.output_file.c_str(), flags, 0666);
    if (redirect_fd != -1) dup2(redirect_fd, STDOUT_FILENO);
  }
  if (cmd.has_error_redirect && !cmd.error_file.empty()) {
    saved_stderr = dup(STDERR_FILENO);
    int flags = O_WRONLY | O_CREAT | (cmd.is_error_append ? O_APPEND : O_TRUNC);
    error_redirect_fd = open(cmd.error_file.c_str(), flags, 0666);
    if (error_redirect_fd != -1) dup2(error_redirect_fd, STDERR_FILENO);
  }
}

void restoreBuiltinRedirects(int& saved_stdout, int& redirect_fd,
                             int& saved_stderr, int& error_redirect_fd) {
  if (saved_stdout != -1) {
    dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdout);
    if (redirect_fd != -1) close(redirect_fd);
    saved_stdout = -1;
    redirect_fd = -1;
  }
  if (saved_stderr != -1) {
    dup2(saved_stderr, STDERR_FILENO);
    close(saved_stderr);
    if (error_redirect_fd != -1) close(error_redirect_fd);
    saved_stderr = -1;
    error_redirect_fd = -1;
  }
}
