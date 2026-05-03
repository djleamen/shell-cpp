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
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <cerrno>
#include <readline/history.h>

using namespace std;
namespace fs = std::filesystem;

bool isBuiltin(const string& cmd) {
  return cmd == "exit" || cmd == "echo" || cmd == "type" || cmd == "pwd"
      || cmd == "cd"   || cmd == "history" || cmd == "jobs" || cmd == "complete"
      || cmd == "declare";
}

string findInPath(const string& program) {
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

void executeBuiltinInChild(const vector<string>& args) {
  string program = args[0];

  if (program == "exit") {
    exit(0);
  }
  else if (program == "echo") {
    for (size_t i = 1; i < args.size(); ++i) {
      if (i > 1) cout << " ";
      cout << args[i];
    }
    cout << endl;
  }
  else if (program == "type" && args.size() > 1) {
    string arg = args[1];
    if (isBuiltin(arg)) {
      cout << arg << " is a shell builtin" << endl;
    } else {
      string path = findInPath(arg);
      if (!path.empty()) {
        cout << arg << " is " << path << endl;
      } else {
        cout << arg << ": not found" << endl;
      }
    }
  }
  else if (program == "pwd") {
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != nullptr) {
      cout << cwd << endl;
    } else {
      cerr << "pwd: error getting current directory" << endl;
    }
  }
  else if (program == "cd" && args.size() > 1) {
    string path = args[1];
    if (path == "~" || path.substr(0, 2) == "~/") {
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
  else if (program == "jobs") {
    exit(0);
  }
  else if (program == "history") {
    if (args.size() > 2 && args[1] == "-r") {
      string filename = args[2];
      ifstream file(filename);
      if (file.is_open()) {
        string line;
        while (getline(file, line)) {
          if (!line.empty()) add_history(line.c_str());
        }
        file.close();
      } else {
        cerr << "history: " << filename << ": No such file or directory" << endl;
      }
    } else if (args.size() > 2 && args[1] == "-w") {
      string filename = args[2];
      ofstream file(filename);
      if (file.is_open()) {
        int start = history_base;
        int end = history_base + history_length;
        for (int i = start; i < end; ++i) {
          HIST_ENTRY* entry = history_get(i);
          if (entry) file << entry->line << endl;
        }
        file.close();
      } else {
        cerr << "history: " << filename << ": cannot create" << endl;
      }
    } else if (args.size() > 2 && args[1] == "-a") {
      string filename = args[2];
      ofstream file(filename, ios::app);
      if (file.is_open()) {
        int start = (last_appended_index == -1) ? history_base : last_appended_index + 1;
        int end = history_base + history_length;
        for (int i = start; i < end; ++i) {
          HIST_ENTRY* entry = history_get(i);
          if (entry) file << entry->line << endl;
        }
        file.close();
        last_appended_index = (history_base + history_length) - 1;
      } else {
        cerr << "history: " << filename << ": cannot create" << endl;
      }
    } else {
      int start = history_base;
      int end = history_base + history_length;
      if (args.size() > 1 && args[1] != "-r" && args[1] != "-w") {
        int n = stoi(args[1]);
        start = max(history_base, end - n);
      }
      for (int i = start; i < end; ++i) {
        HIST_ENTRY* entry = history_get(i);
        if (entry) cout << "    " << i << "  " << entry->line << endl;
      }
    }
  }
  exit(0);
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
    vector<char*> argv;
    argv_storage.reserve(args.size());
    argv.reserve(args.size() + 1);
    for (const auto& arg : args) {
      argv_storage.emplace_back(arg.begin(), arg.end());
      argv_storage.back().push_back('\0');
      argv.push_back(argv_storage.back().data());
    }
    argv.push_back(nullptr);
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
        vector<char*> argv;
        argv_storage.reserve(cmd.args.size());
        argv.reserve(cmd.args.size() + 1);
        for (const auto& arg : cmd.args) {
          argv_storage.emplace_back(arg.begin(), arg.end());
          argv_storage.back().push_back('\0');
          argv.push_back(argv_storage.back().data());
        }
        argv.push_back(nullptr);
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
