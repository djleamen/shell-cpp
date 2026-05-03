/**
 * @file main.cpp
 * @brief A POSIX-compatible interactive shell - REPL entry point.
 *
 * All subsystems are in their own modules:
 *   globals.h/cpp      - shared state and built-in name table
 *   jobs.h/cpp         - background-job tracking and SIGCHLD handling
 *   parser.h/cpp       - command-line tokeniser and pipeline parser
 *   executor.h/cpp     - built-in / external command execution
 *   completion.h/cpp   - GNU Readline tab-completion hooks
 */

#include "globals.h"
#include "jobs.h"
#include "parser.h"
#include "executor.h"
#include "completion.h"

#include <iostream>
#include <string>
#include <fstream>
#include <csignal>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <algorithm>
#include <vector>

using namespace std;

/**
 * @brief Shell entry point implementing the Read-Eval-Print Loop (REPL).
 *
 * Startup:
 *  1. Enables immediate flushing of cout and cerr.
 *  2. Registers command_completion and display_matches_hook with readline.
 *  3. Installs sigchld_handler via sigaction(SIGCHLD).
 *  4. Loads command history from HISTFILE (if set).
 *
 * REPL loop:
 *  1. Reap  - notify the user of any completed background jobs.
 *  2. Read  - display "$ " prompt and read a line via readline; exit on EOF.
 *  3. Eval  - parse the line with parsePipeline() and dispatch:
 *               - Empty input: skip.
 *               - Pipeline (|): call executePipeline().
 *               - Background job (&): fork without waiting, register in bg_jobs.
 *               - Built-in commands: handle inline with I/O redirection scaffolding.
 *               - External commands: call executeProgram().
 *  4. Restore - undo any stdout/stderr redirection done for built-ins.
 *
 * Shutdown:
 *  - Writes the full readline history to HISTFILE (if set).
 *
 * @return 0 on normal exit.
 */
int main() {
  cout << unitbuf;
  cerr << unitbuf;

  rl_attempted_completion_function = command_completion;
  rl_completion_display_matches_hook = display_matches_hook;

  struct sigaction sa;
  sa.sa_handler = sigchld_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
  sigaction(SIGCHLD, &sa, nullptr);

  string histfile;
  const char* histfile_env = getenv("HISTFILE");
  if (histfile_env) {
    histfile = histfile_env;
  }

  if (!histfile.empty()) {
    ifstream file(histfile);
    if (file.is_open()) {
      string line;
      while (getline(file, line)) {
        if (!line.empty()) {
          add_history(line.c_str());
        }
      }
      file.close();
    }
  }

  while (true) {
    reapJobs();
    char* input = readline("$ ");

    if (!input) {
      break;
    }

    string command(input);
    free(input);

    if (!command.empty()) {
      add_history(command.c_str());
    }

    PipelineInfo pipeline = parsePipeline(command);

    if (pipeline.commands.empty() ||
        (pipeline.commands.size() == 1 && pipeline.commands[0].args.empty())) {
      continue;
    }

    if (pipeline.has_pipe && pipeline.commands.size() > 1) {
      executePipeline(pipeline.commands);
      continue;
    }

    CommandInfo cmd_info = pipeline.commands[0];
    vector<string>& args = cmd_info.args;

    bool run_in_bg = false;
    if (!args.empty() && args.back() == "&") {
      run_in_bg = true;
      args.pop_back();
      if (args.empty()) continue;
    }

    string program = args[0];

    if (run_in_bg) {
      string path = findInPath(program);
      if (!path.empty()) {
        pid_t pid = fork();
        if (pid == 0) {
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
          int job_num = nextJobNumber();
          bg_jobs.push_back({job_num, pid, command});
          cout << "[" << job_num << "] " << pid << endl;
        } else {
          cerr << "Fork failed" << endl;
        }
      } else {
        cout << program << ": command not found" << endl;
      }
      continue;
    }

    int saved_stdout = -1;
    int redirect_fd = -1;
    int saved_stderr = -1;
    int error_redirect_fd = -1;

    if (cmd_info.has_redirect && !cmd_info.output_file.empty()) {
      saved_stdout = dup(STDOUT_FILENO);
      int flags = O_WRONLY | O_CREAT | (cmd_info.is_append ? O_APPEND : O_TRUNC);
      redirect_fd = open(cmd_info.output_file.c_str(), flags, 0666);
      if (redirect_fd != -1) {
        dup2(redirect_fd, STDOUT_FILENO);
      }
    }

    if (cmd_info.has_error_redirect && !cmd_info.error_file.empty()) {
      saved_stderr = dup(STDERR_FILENO);
      int flags = O_WRONLY | O_CREAT | (cmd_info.is_error_append ? O_APPEND : O_TRUNC);
      error_redirect_fd = open(cmd_info.error_file.c_str(), flags, 0666);
      if (error_redirect_fd != -1) {
        dup2(error_redirect_fd, STDERR_FILENO);
      }
    }

    if (program == "exit") {
      break;
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
    else if (program == "history") {
      if (args.size() > 2 && args[1] == "-r") {
        string filename = args[2];
        ifstream file(filename);
        if (file.is_open()) {
          string line;
          while (getline(file, line)) {
            if (!line.empty()) {
              add_history(line.c_str());
            }
          }
          file.close();
        } else {
          cerr << "history: " << filename << ": No such file or directory" << endl;
        }
      } else if (args.size() > 2 && args[1] == "-a") {
        string filename = args[2];
        ofstream file(filename, ios::app);
        if (file.is_open()) {
          int start = (last_appended_index == -1) ? history_base : last_appended_index + 1;
          int end = history_base + history_length;
          for (int i = start; i < end; ++i) {
            HIST_ENTRY* entry = history_get(i);
            if (entry) {
              file << entry->line << endl;
            }
          }
          file.close();
          last_appended_index = (history_base + history_length) - 1;
        } else {
          cerr << "history: " << filename << ": cannot create" << endl;
        }
      } else if (args.size() > 2 && args[1] == "-w") {
        string filename = args[2];
        ofstream file(filename);
        if (file.is_open()) {
          int start = history_base;
          int end = history_base + history_length;
          for (int i = start; i < end; ++i) {
            HIST_ENTRY* entry = history_get(i);
            if (entry) {
              file << entry->line << endl;
            }
          }
          file.close();
        } else {
          cerr << "history: " << filename << ": cannot create" << endl;
        }
      } else {
        int start = history_base;
        int end = history_base + history_length;

        if (args.size() > 1 && args[1] != "-r" && args[1] != "-w" && args[1] != "-a") {
          int n = stoi(args[1]);
          start = max(history_base, end - n);
        }

        for (int i = start; i < end; ++i) {
          HIST_ENTRY* entry = history_get(i);
          if (entry) {
            cout << "    " << i << "  " << entry->line << endl;
          }
        }
      }
    }
    else if (program == "jobs") {
      {
        int wstatus;
        pid_t p;
        while ((p = waitpid(-1, &wstatus, WNOHANG)) > 0) {
          for (auto& job : bg_jobs) {
            if (job.pid == p && (WIFEXITED(wstatus) || WIFSIGNALED(wstatus))) {
              job.done = true;
              break;
            }
          }
        }
      }
      for (int i = 0; i < (int)bg_jobs.size(); i++) {
        if (!bg_jobs[i].done) {
          int wstatus;
          pid_t result = waitpid(bg_jobs[i].pid, &wstatus, WNOHANG);
          if ((result > 0 && (WIFEXITED(wstatus) || WIFSIGNALED(wstatus))) ||
              (result < 0 && errno == ECHILD)) {
            bg_jobs[i].done = true;
          }
        }
      }
      vector<int> done_indices;
      for (int i = 0; i < (int)bg_jobs.size(); i++) {
        if (bg_jobs[i].done) {
          done_indices.push_back(i);
        }
      }
      int n = bg_jobs.size();
      for (int i = 0; i < n; i++) {
        const auto& job = bg_jobs[i];
        char marker = ' ';
        if (i == n - 1) marker = '+';
        else if (i == n - 2) marker = '-';
        bool is_done = find(done_indices.begin(), done_indices.end(), i) != done_indices.end();
        string status_str = is_done ? "Done" : "Running";
        status_str.resize(24, ' ');
        string cmd = job.command;
        if (is_done) {
          if (cmd.size() >= 2 && cmd.substr(cmd.size() - 2) == " &") {
            cmd = cmd.substr(0, cmd.size() - 2);
          }
        }
        cout << "[" << job.job_number << "]" << marker << "  " << status_str << cmd << endl;
      }
      for (int i = done_indices.size() - 1; i >= 0; i--) {
        bg_jobs.erase(bg_jobs.begin() + done_indices[i]);
      }
    }
    else if (program == "complete") {
      if (args.size() > 3 && args[1] == "-C") {
        completion_registry[args[3]] = args[2];
      } else if (args.size() > 2 && args[1] == "-r") {
        completion_registry.erase(args[2]);
      } else if (args.size() > 2 && args[1] == "-p") {
        string cmd = args[2];
        auto it = completion_registry.find(cmd);
        if (it != completion_registry.end()) {
          cout << "complete -C '" << it->second << "' " << cmd << endl;
        } else {
          cerr << "complete: " << cmd << ": no completion specification" << endl;
        }
      }
    }
    else if (program == "declare") {
      if (args.size() > 2 && args[1] == "-p") {
        string varname = args[2];
        cerr << "declare: " << varname << ": not found" << endl;
      }
    }
    else if (program == "cd" && args.size() > 1) {
      string path = args[1];

      if (path == "~" || path.substr(0, 2) == "~/") {
        string home;
        const char* home_env = getenv("HOME");
        if (home_env) {
          home = home_env;
          if (path == "~") {
            path = home;
          } else {
            path = home + path.substr(1);
          }
        }
      }

      if (chdir(path.c_str()) != 0) {
        cout << "cd: " << path << ": No such file or directory" << endl;
      }
    }
    else {
      if (saved_stdout != -1) {
        dup2(saved_stdout, STDOUT_FILENO);
        close(saved_stdout);
        if (redirect_fd != -1) close(redirect_fd);
        saved_stdout = -1;
      }

      if (saved_stderr != -1) {
        dup2(saved_stderr, STDERR_FILENO);
        close(saved_stderr);
        if (error_redirect_fd != -1) close(error_redirect_fd);
        saved_stderr = -1;
      }

      string path = findInPath(program);
      if (!path.empty()) {
        executeProgram(path, args,
                      cmd_info.has_redirect ? cmd_info.output_file : "",
                      cmd_info.is_append,
                      cmd_info.has_error_redirect ? cmd_info.error_file : "",
                      cmd_info.is_error_append);
      } else {
        cout << program << ": command not found" << endl;
      }
    }

    if (saved_stdout != -1) {
      dup2(saved_stdout, STDOUT_FILENO);
      close(saved_stdout);
      if (redirect_fd != -1) close(redirect_fd);
    }

    if (saved_stderr != -1) {
      dup2(saved_stderr, STDERR_FILENO);
      close(saved_stderr);
      if (error_redirect_fd != -1) close(error_redirect_fd);
    }
  }

  if (!histfile.empty()) {
    ofstream file(histfile);
    if (file.is_open()) {
      int start = history_base;
      int end = history_base + history_length;
      for (int i = start; i < end; ++i) {
        HIST_ENTRY* entry = history_get(i);
        if (entry) {
          file << entry->line << endl;
        }
      }
      file.close();
    }
  }

  return 0;
}
