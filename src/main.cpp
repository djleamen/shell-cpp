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
#include <readline/readline.h>
#include <readline/history.h>
#include <algorithm>
#include <memory>
#include <string_view>
#include <vector>

using namespace std;

static void initShell() {
  cout << unitbuf;
  cerr << unitbuf;
  rl_attempted_completion_function = command_completion;
#ifdef __APPLE__
  // macOS readline headers type this as VFunction* (void(*)()) — cast required.
  rl_completion_display_matches_hook = reinterpret_cast<VFunction*>(display_matches_hook);
#else
  // Linux/GCC: typed as rl_compdisp_func_t* (void(*)(char**,int,int)) — plain assignment.
  rl_completion_display_matches_hook = display_matches_hook;
#endif
  struct sigaction sa;
  sa.sa_handler = sigchld_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
  sigaction(SIGCHLD, &sa, nullptr);
}

static string getHistfile() {
  const char* e = getenv("HISTFILE");
  return e ? string(e) : string();
}

static void loadHistory(const string& histfile) {
  if (histfile.empty()) return;
  ifstream file(histfile);
  if (!file.is_open()) return;
  string line;
  while (getline(file, line)) {
    if (!line.empty()) add_history(line.c_str());
  }
}

static void saveHistory(const string& histfile) {
  if (histfile.empty()) return;
  ofstream file(histfile);
  if (!file.is_open()) return;
  for (int i = history_base; i < history_base + history_length; ++i) {
    const HIST_ENTRY* entry = history_get(i);
    if (entry) file << entry->line << endl;
  }
}

static void runEcho(const vector<string>& args) {
  for (size_t i = 1; i < args.size(); ++i) {
    if (i > 1) cout << " ";
    cout << args[i];
  }
  cout << endl;
}

static void runType(const vector<string>& args) {
  if (args.size() <= 1) return;
  const string& arg = args[1];
  if (isBuiltin(arg)) { cout << arg << " is a shell builtin" << endl; return; }
  string path = findInPath(arg);
  if (!path.empty()) cout << arg << " is " << path << endl;
  else               cout << arg << ": not found" << endl;
}

static void runPwd() {
  string cwd(1024, '\0');
  if (getcwd(cwd.data(), cwd.size()) != nullptr) cout << cwd.c_str() << endl;
  else cerr << "pwd: error getting current directory" << endl;
}

static void runHistoryRead(const string& filename) {
  ifstream file(filename);
  if (!file.is_open()) { cerr << "history: " << filename << ": No such file or directory" << endl; return; }
  string line;
  while (getline(file, line)) {
    if (!line.empty()) add_history(line.c_str());
  }
}

static void runHistoryAppend(const string& filename) {
  ofstream file(filename, ios::app);
  if (!file.is_open()) { cerr << "history: " << filename << ": cannot create" << endl; return; }
  int start = (last_appended_index() == -1) ? history_base : last_appended_index() + 1;
  int end = history_base + history_length;
  for (int i = start; i < end; ++i) {
    const HIST_ENTRY* entry = history_get(i);
    if (entry) file << entry->line << endl;
  }
  last_appended_index() = (history_base + history_length) - 1;
}

static void runHistoryWrite(const string& filename) {
  ofstream file(filename);
  if (!file.is_open()) { cerr << "history: " << filename << ": cannot create" << endl; return; }
  for (int i = history_base; i < history_base + history_length; ++i) {
    const HIST_ENTRY* entry = history_get(i);
    if (entry) file << entry->line << endl;
  }
}

static void runHistoryList(const vector<string>& args) {
  int end = history_base + history_length;
  int start = history_base;
  if (args.size() > 1 && args[1] != "-r" && args[1] != "-w" && args[1] != "-a") {
    start = max(history_base, end - stoi(args[1]));
  }
  for (int i = start; i < end; ++i) {
    const HIST_ENTRY* entry = history_get(i);
    if (entry) cout << "    " << i << "  " << entry->line << endl;
  }
}

static void runHistory(const vector<string>& args) {
  if (args.size() > 2 && args[1] == "-r") { runHistoryRead(args[2]);   return; }
  if (args.size() > 2 && args[1] == "-a") { runHistoryAppend(args[2]); return; }
  if (args.size() > 2 && args[1] == "-w") { runHistoryWrite(args[2]);  return; }
  runHistoryList(args);
}

static void runComplete(const vector<string>& args) {
  if (args.size() > 3 && args[1] == "-C") { completion_registry()[args[3]] = args[2]; return; }
  if (args.size() > 2 && args[1] == "-r") { completion_registry().erase(args[2]);     return; }
  if (args.size() > 2 && args[1] == "-p") {
    const string& cmd = args[2];
    auto it = completion_registry().find(cmd);
    if (it != completion_registry().end()) cout << "complete -C '" << it->second << "' " << cmd << endl;
    else cerr << "complete: " << cmd << ": no completion specification" << endl;
  }
}

static void runDeclareShow(const string& varname) {
  auto it = shell_variables().find(varname);
  if (it != shell_variables().end()) cout << "declare -- " << it->first << "=\"" << it->second << "\"" << endl;
  else cerr << "declare: " << varname << ": not found" << endl;
}

static void runDeclareSet(const string& assignment) {
  size_t eq = assignment.find('=');
  if (eq == string::npos) return;
  string varname = assignment.substr(0, eq);
  bool valid = !varname.empty()
    && (isalpha(static_cast<unsigned char>(varname[0])) || varname[0] == '_')
    && all_of(varname.begin() + 1, varname.end(),
              [](unsigned char c){ return isalnum(c) || c == '_'; });
  if (!valid) cerr << "declare: `" << assignment << "': not a valid identifier" << endl;
  else        shell_variables()[varname] = assignment.substr(eq + 1);
}

static void runDeclare(const vector<string>& args) {
  if (args.size() > 1 && args[1] == "-p") {
    if (args.size() > 2) runDeclareShow(args[2]);
    return;
  }
  if (args.size() > 1) runDeclareSet(args[1]);
}

static void runCd(const vector<string>& args) {
  if (args.size() <= 1) return;
  string path = args[1];
  if (path == "~" || path.starts_with("~/")) {
    const char* home_env = getenv("HOME");
    if (home_env) {
      string home = home_env;
      path = (path == "~") ? home : home + path.substr(1);
    }
  }
  if (chdir(path.c_str()) != 0) cout << "cd: " << path << ": No such file or directory" << endl;
}

static void runBackground(const string& program, const vector<string>& args, const string& command) {
  string path = findInPath(program);
  if (path.empty()) { cout << program << ": command not found" << endl; return; }
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
    bg_jobs().emplace_back(job_num, pid, command);
    cout << "[" << job_num << "] " << pid << endl;
  } else {
    cerr << "Fork failed" << endl;
  }
}

static bool dispatchBuiltin(string_view program, const CommandInfo& cmd_info) {
  const vector<string>& args = cmd_info.args;
  if (program == "exit")    return true;
  if (program == "echo")    { runEcho(args);     return false; }
  if (program == "type")    { runType(args);     return false; }
  if (program == "pwd")     { runPwd();          return false; }
  if (program == "history") { runHistory(args);  return false; }
  if (program == "jobs")    { listJobs();        return false; }
  if (program == "complete"){ runComplete(args); return false; }
  if (program == "declare") { runDeclare(args);  return false; }
  if (program == "cd")      { runCd(args);       return false; }
  return false;
}

static bool processCommand(const string& command) {
  PipelineInfo pipeline = parsePipeline(command);
  if (pipeline.commands.empty() ||
      (pipeline.commands.size() == 1 && pipeline.commands[0].args.empty())) {
    return false;
  }
  if (pipeline.has_pipe && pipeline.commands.size() > 1) {
    executePipeline(pipeline.commands);
    return false;
  }

  CommandInfo cmd_info = pipeline.commands[0];
  vector<string>& args = cmd_info.args;

  if (!args.empty() && args.back() == "&") {
    args.pop_back();
    if (args.empty()) return false;
    expandArgs(args);
    runBackground(args[0], args, command);
    return false;
  }

  expandArgs(args);
  string program = args[0];

  int saved_stdout = -1;
  int redirect_fd = -1;
  int saved_stderr = -1;
  int error_redirect_fd = -1;
  setupBuiltinRedirects(cmd_info, saved_stdout, redirect_fd, saved_stderr, error_redirect_fd);

  bool should_exit = false;
  if (isBuiltin(program) || program == "exit") {
    should_exit = dispatchBuiltin(program, cmd_info);
  } else {
    restoreBuiltinRedirects(saved_stdout, redirect_fd, saved_stderr, error_redirect_fd);
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
  restoreBuiltinRedirects(saved_stdout, redirect_fd, saved_stderr, error_redirect_fd);
  return should_exit;
}

int main() {
  initShell();
  string histfile = getHistfile();
  loadHistory(histfile);

  bool should_exit = false;
  do {
    reapJobs();
    unique_ptr<char, decltype(&free)> raw(readline("$ "), free);
    if (!raw) break;
    string command(raw.get());
    if (!command.empty()) add_history(command.c_str());
    should_exit = processCommand(command);
  } while (!should_exit);

  saveHistory(histfile);
  return 0;
}
