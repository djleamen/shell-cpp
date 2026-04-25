/**
 * @file main.cpp
 * @brief A POSIX-compatible interactive shell implementation built for the
 *        CodeCrafters "Build Your Own Shell" challenge (C++23).
 *
 * Features:
 *  - Built-in commands: echo, exit, type, pwd, cd, history, jobs, complete
 *  - External command execution via execv()
 *  - I/O redirection (>, >>, 2>, 2>>) for both stdout and stderr
 *  - Pipeline support (cmd1 | cmd2 | ...)
 *  - Background job execution and tracking (&)
 *  - GNU Readline integration for line editing and tab completion
 *  - Programmable completion via `complete -C`
 *  - History management with HISTFILE persistence
 *  - Single-quote, double-quote, and backslash escape handling
 */

#include <iostream>
#include <string>
#include <sstream>
#include <filesystem>
#include <cstdlib>
#include <vector>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <fstream>
#include <readline/readline.h>
#include <readline/history.h>
#include <map>
#include <csignal>
#include <cerrno>

using namespace std;
namespace fs = std::filesystem;

/**
 * @brief Tracks the readline history index of the last entry that was
 *        appended to the history file via `history -a`. Initialized to -1
 *        to indicate that no append has occurred yet in the current session.
 */
int last_appended_index = -1;

/**
 * @brief Maps a command name to the path of its external completion script.
 *        Populated by `complete -C <script> <cmd>` and consumed by the
 *        readline completion machinery.
 */
map<string, string> completion_registry;

/**
 * @brief Represents a single background job launched with `&`.
 *
 * @var job_number  Shell job number shown in `[N]` brackets (1-based).
 * @var pid         OS process ID of the background child.
 * @var command     The raw command string as entered by the user (including `&`).
 * @var done        Set to true when the process has exited or been signalled.
 */
struct BackgroundJob {
  int job_number;
  pid_t pid;
  string command;
  bool done = false;
};

/** @brief The live list of background jobs managed by this shell session. */
vector<BackgroundJob> bg_jobs;

/**
 * @brief Returns the lowest positive integer not already in use as a job number.
 *
 * Scans @c bg_jobs linearly to find gaps so that job numbers are recycled in
 * ascending order after completed jobs are reaped.
 *
 * @return The next available job number (>= 1).
 */
int nextJobNumber() {
  int num = 1;
  while (true) {
    bool used = false;
    for (const auto& job : bg_jobs) {
      if (job.job_number == num) { used = true; break; }
    }
    if (!used) return num;
    num++;
  }
}

/**
 * @brief SIGCHLD signal handler that non-blockingly reaps exited/signalled
 *        background children and marks their corresponding @c BackgroundJob
 *        entries as done.
 *
 * Uses @c waitpid with @c WNOHANG in a loop so that bursts of simultaneous
 * child exits are all handled.  @c errno is saved and restored because signal
 * handlers must not clobber it.
 *
 * @param[in] (unnamed)  Signal number — always SIGCHLD; not used.
 */
void sigchld_handler(int) {
  int saved_errno = errno;
  int wstatus;
  pid_t pid;
  while ((pid = waitpid(-1, &wstatus, WNOHANG)) > 0) {
    for (auto& job : bg_jobs) {
      if (job.pid == pid && (WIFEXITED(wstatus) || WIFSIGNALED(wstatus))) {
        job.done = true;
        break;
      }
    }
  }
  errno = saved_errno;
}

/**
 * @brief Reaps all completed background jobs, prints their completion status
 *        to stdout, and removes them from @c bg_jobs.
 *
 * Called at the beginning of each REPL iteration so that the user sees
 * completion notices before the next prompt is displayed.  The function:
 *  1. Polls each non-done job with @c waitpid(WNOHANG).
 *  2. Prints a line like `[1]+  Done                    sleep 5` for each
 *     finished job, using `+` for the most-recently-added job and `-` for
 *     the second-most-recently-added.
 *  3. Erases done entries from @c bg_jobs (back-to-front to preserve indices).
 */
void reapJobs() {
  vector<int> done_indices;
  for (int i = 0; i < (int)bg_jobs.size(); i++) {
    if (!bg_jobs[i].done) {
      int wstatus;
      pid_t result = waitpid(bg_jobs[i].pid, &wstatus, WNOHANG);
      if ((result > 0 && (WIFEXITED(wstatus) || WIFSIGNALED(wstatus))) ||
          (result < 0 && errno == ECHILD)) {
        bg_jobs[i].done = true;
      }
    }
    if (bg_jobs[i].done) {
      done_indices.push_back(i);
    }
  }
  int n = bg_jobs.size();
  for (int i = 0; i < n; i++) {
    if (find(done_indices.begin(), done_indices.end(), i) != done_indices.end()) {
      const auto& job = bg_jobs[i];
      char marker = ' ';
      if (i == n - 1) marker = '+';
      else if (i == n - 2) marker = '-';
      string status_str = "Done";
      status_str.resize(24, ' ');
      string cmd = job.command;
      if (cmd.size() >= 2 && cmd.substr(cmd.size() - 2) == " &") {
        cmd = cmd.substr(0, cmd.size() - 2);
      }
      cout << "[" << job.job_number << "]" << marker << "  " << status_str << cmd << endl;
    }
  }
  for (int i = done_indices.size() - 1; i >= 0; i--) {
    bg_jobs.erase(bg_jobs.begin() + done_indices[i]);
  }
}

/**
 * @brief Null-terminated array of built-in command names used by the
 *        readline tab-completion generator (@c command_generator) and by
 *        @c isBuiltin().
 */
const char* builtin_commands[] = {
  "echo",
  "exit",
  "type",
  "pwd",
  "cd",
  "history",
  "jobs",
  "complete",
  nullptr
};

/**
 * @brief Readline completion generator that yields command names matching
 *        a given prefix.
 *
 * On the first call for a new completion attempt (@p state == 0) it:
 *  - Collects all executable files reachable via PATH whose names start with
 *    @p text.
 *  - Resets the static iteration state.
 *
 * Subsequent calls (@p state > 0) continue iterating through built-in
 * command names first, then PATH executables, returning @c nullptr when
 * the list is exhausted.
 *
 * @param[in] text   The partial command name to complete.
 * @param[in] state  0 on the first call; non-zero on successive calls.
 * @return  A heap-allocated copy of the next matching name, or @c nullptr
 *          when no more matches are available.  The caller owns the memory.
 */
char* command_generator(const char* text, int state) {
  static int list_index;
  static size_t len;
  static string search_text;  // Store as string for safety
  static vector<string> path_executables;
  static size_t path_exec_index;
  static bool builtins_done;
  
  if (!state) {
    list_index = 0;
    search_text = text ? text : "";
    len = search_text.length();
    path_executables.clear();
    path_exec_index = 0;
    builtins_done = false;
    
    string path_str;
    const char* path_env = getenv("PATH");
    if (path_env) {
      path_str = path_env;
      stringstream ss(path_str);
      string dir;
      
      while (getline(ss, dir, ':')) {
        if (!fs::exists(dir)) continue;
        
        try {
          for (const auto& entry : fs::directory_iterator(dir)) {
            if (entry.is_regular_file()) {
              string filename = entry.path().filename().string();
              if (filename.length() >= len && filename.substr(0, len) == search_text) {
                auto perms = entry.status().permissions();
                if ((perms & fs::perms::owner_exec) != fs::perms::none ||
                    (perms & fs::perms::group_exec) != fs::perms::none ||
                    (perms & fs::perms::others_exec) != fs::perms::none) {
                  // Avoid duplicates
                  if (find(path_executables.begin(), path_executables.end(), filename) == path_executables.end()) {
                    path_executables.push_back(filename);
                  }
                }
              }
            }
          }
        } catch (...) {
          // Skip directories that cause errors...
        }
      }
    }
  }
  
  if (!builtins_done) {
    while (builtin_commands[list_index]) {
      const char* name = builtin_commands[list_index++];
      string cmd_name(name);
      if (cmd_name.length() >= len && cmd_name.substr(0, len) == search_text) {
        return strdup(name);
      }
    }
    builtins_done = true;
  }
  
  if (path_exec_index < path_executables.size()) {
    return strdup(path_executables[path_exec_index++].c_str());
  }
  
  return nullptr;
}

/**
 * @brief Readline completion generator that yields filesystem entries
 *        (files and directories) matching a given path prefix.
 *
 * On the first call (@p state == 0) it splits @p text into a directory part
 * and a filename prefix, iterates the directory with
 * @c std::filesystem::directory_iterator, and populates an internal match
 * list.  Directory entries are returned with a trailing `/`; for those,
 * @c rl_completion_append_character is set to `\0` so readline does not add
 * a trailing space.
 *
 * @param[in] text   The partial path to complete.
 * @param[in] state  0 on the first call; non-zero on successive calls.
 * @return  A heap-allocated copy of the next matching path, or @c nullptr
 *          when exhausted.  The caller owns the memory.
 */
char* filename_generator(const char* text, int state) {
  static vector<string> matches;
  static size_t match_index;

  if (!state) {
    matches.clear();
    match_index = 0;

    string input(text ? text : "");

    string dir_path;
    string file_prefix;
    size_t last_slash = input.rfind('/');
    if (last_slash != string::npos) {
      dir_path = input.substr(0, last_slash + 1);
      file_prefix = input.substr(last_slash + 1);
    } else {
      dir_path = "";
      file_prefix = input;
    }

    string search_dir = dir_path.empty() ? "." : dir_path;
    size_t prefix_len = file_prefix.length();

    try {
      for (const auto& entry : fs::directory_iterator(search_dir)) {
        string filename = entry.path().filename().string();
        if (filename.length() >= prefix_len && filename.substr(0, prefix_len) == file_prefix) {
          string match = dir_path + filename;
          if (fs::is_directory(entry.status())) {
            match += "/";
          }
          matches.push_back(match);
        }
      }
    } catch (...) {
      // Skip errors
    }
  }

  if (match_index < matches.size()) {
    string match = matches[match_index++];
    if (!match.empty() && match.back() == '/') {
      rl_completion_append_character = '\0';
    } else {
      rl_completion_append_character = ' ';
    }
    return strdup(match.c_str());
  }

  return nullptr;
}

/**
 * @brief Staging buffer populated by @c command_completion when a
 *        programmable completer script produces candidates.  The generator
 *        @c completer_generator drains this vector one entry per call.
 */
vector<string> completer_results;

/**
 * @brief Readline completion generator that iterates over
 *        @c completer_results, which were filled by an external completion
 *        script invoked from @c command_completion.
 *
 * @param[in] text   Ignored; matching was already done by the external script.
 * @param[in] state  0 on the first call; non-zero on successive calls.
 * @return  A heap-allocated copy of the next candidate, or @c nullptr when
 *          exhausted.  The caller owns the memory.
 */
char* completer_generator(const char* text, int state) {
  static size_t idx;
  if (!state) {
    idx = 0;
  }
  while (idx < completer_results.size()) {
    string candidate = completer_results[idx++];
    rl_completion_append_character = ' ';
    return strdup(candidate.c_str());
  }
  return nullptr;
}

/**
 * @brief Custom readline completion function registered via
 *        @c rl_attempted_completion_function.
 *
 * Dispatch logic:
 *  1. If the cursor is at the start of the line (@p start == 0), delegate to
 *     @c command_generator for command-name completion.
 *  2. If the command at the beginning of the line has an entry in
 *     @c completion_registry, invoke the registered external script with
 *     @c COMP_LINE and @c COMP_POINT set, parse its output into
 *     @c completer_results, and delegate to @c completer_generator.
 *  3. Otherwise fall back to filename completion via @c filename_generator.
 *
 * @param[in] text   The word under the cursor being completed.
 * @param[in] start  Byte offset of @p text in the readline input buffer.
 * @param[in] end    Byte offset of the end of @p text (unused).
 * @return  Array of heap-allocated completion strings as expected by readline,
 *          or @c nullptr to let readline perform its default filename
 *          completion (not reached because we always set
 *          @c rl_attempted_completion_over).
 */
char** command_completion(const char* text, int start, int end) {
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
    completer_results.clear();

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

    // COMP_LINE is the full line up to the cursor (start = cursor position here)
    string comp_line = line.substr(0, start) + string(text);
    // COMP_POINT is the byte index of the cursor (end of the current partial word)
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
        if (!out.empty()) completer_results.push_back(out);
      }
      pclose(fp);
    }
    if (!completer_results.empty()) {
      rl_attempted_completion_over = 1;
      return rl_completion_matches(text, completer_generator);
    }
  }

  rl_attempted_completion_over = 1;
  return rl_completion_matches(text, filename_generator);
}

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
  vector<string> args;
  string output_file;
  bool has_redirect;
  bool is_append;
  string error_file;
  bool has_error_redirect;
  bool is_error_append;
};

/**
 * @brief Parsed representation of a complete input line, potentially
 *        containing multiple commands connected by `|`.
 *
 * @var commands   Ordered list of @c CommandInfo objects, one per
 *                 pipe-separated segment.
 * @var has_pipe   True when at least one `|` operator separated the commands.
 */
struct PipelineInfo {
  vector<CommandInfo> commands;
  bool has_pipe;
};

/**
 * @brief Parses a raw command string into a @c PipelineInfo structure.
 *
 * The function performs two passes:
 *  1. **Pipe splitting**: splits on unquoted `|` characters to produce
 *     individual command strings, respecting single-quote, double-quote, and
 *     backslash escaping so that `echo "a|b"` is not split.
 *  2. **Token parsing**: for each command string, tokenizes into arguments
 *     while applying the full POSIX quoting rules:
 *       - Outside quotes: `\` escapes the next character literally.
 *       - Inside double quotes: only `\"` and `\\` are escape sequences;
 *         other `\X` sequences are kept verbatim.
 *       - Inside single quotes: all characters are literal.
 *     Redirection operators (`>`, `>>`, `1>`, `1>>`, `2>`, `2>>`) and their
 *     target filenames are extracted into the corresponding @c CommandInfo
 *     fields and removed from the argument list.
 *
 * @param[in] command  The raw command line as entered by the user.
 * @return A fully populated @c PipelineInfo ready for execution.
 */
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

string findInPath(const string& program);

/**
 * @brief Returns true when @p cmd is one of the shell's built-in commands.
 *
 * Built-ins are handled directly by the shell process rather than by forking
 * a child, which allows them to affect the shell's own state (e.g., `cd`
 * changes the working directory, `exit` terminates the shell).
 *
 * @param[in] cmd  Command name to test.
 * @return @c true if @p cmd is a built-in; @c false otherwise.
 */
bool isBuiltin(const string& cmd) {
  return cmd == "exit" || cmd == "echo" || cmd == "type" || cmd == "pwd" || cmd == "cd" || cmd == "history" || cmd == "jobs" || cmd == "complete";
}

/**
 * @brief Executes a built-in command inside a forked child process.
 *
 * Used exclusively by @c executePipeline so that built-ins can participate
 * in a pipeline (their output goes through the pipe) while the parent shell
 * process remains unaffected.  The function always terminates the child via
 * @c exit() after the command runs.
 *
 * Built-ins handled: `exit`, `echo`, `type`, `pwd`, `cd`, `jobs`, `history`.
 * The `complete` built-in is not meaningful inside a pipeline and exits
 * silently.
 *
 * @param[in] args  Tokenized arguments where @c args[0] is the command name.
 */
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
          if (!line.empty()) {
            add_history(line.c_str());
          }
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
          if (entry) {
            file << entry->line << endl;
          }
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
          if (entry) {
            file << entry->line << endl;
          }
        }
        file.close();
        last_appended_index = end - 1;
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
        if (entry) {
          cout << "    " << i << "  " << entry->line << endl;
        }
      }
    }
  }
  exit(0);
}

/**
 * @brief Searches the directories listed in the PATH environment variable
 *        for an executable named @p program.
 *
 * Each colon-separated directory is tried in order.  A candidate is accepted
 * when:
 *  - The file exists.
 *  - The owner-execute permission bit is set.
 *
 * @param[in] program  The bare executable name to look up (no path separators).
 * @return The absolute path to the first match, or an empty string if not found.
 */
string findInPath(const string& program) {
  string path_str;
  const char* path_env = getenv("PATH");
  if (path_env) {
    path_str = path_env;
  } else {
    return "";
  }
  stringstream ss(path_str);
  string dir;
  
  while (getline(ss, dir, ':')) {
    fs::path full_path = fs::path(dir) / program;
    if (fs::exists(full_path) && (fs::status(full_path).permissions() & fs::perms::owner_exec) != fs::perms::none) {
      return full_path.string();
    }
  }
  return "";
}

/**
 * @brief Forks a child process and runs an external program via @c execv().
 *
 * If @p output_file is non-empty the child redirects its stdout to that file
 * (truncating or appending according to @p is_append).  Similarly for
 * @p error_file / @p is_error_append on stderr.
 *
 * The parent blocks in @c waitpid until the child exits, making this a
 * synchronous (foreground) execution helper.  For background execution see
 * the `&` handling in @c main().
 *
 * @param[in] path             Absolute path to the executable.
 * @param[in] args             Argument vector; @c args[0] should be the program name.
 * @param[in] output_file      Path to redirect stdout; empty string disables redirection.
 * @param[in] is_append        When true, stdout is opened in append mode (`>>`).
 * @param[in] error_file       Path to redirect stderr; empty string disables redirection.
 * @param[in] is_error_append  When true, stderr is opened in append mode (`2>>`).
 */
void executeProgram(const string& path, const vector<string>& args, const string& output_file = "", bool is_append = false, const string& error_file = "", bool is_error_append = false) {
  pid_t pid = fork();
  
  if (pid == 0) {
    if (!output_file.empty()) {
      int flags = O_WRONLY | O_CREAT | (is_append ? O_APPEND : O_TRUNC);
      int fd = open(output_file.c_str(), flags, 0644);
      if (fd == -1) {
        cerr << "Failed to open " << output_file << " for writing" << endl;
        exit(1);
      }
      dup2(fd, STDOUT_FILENO);
      close(fd);
    }
    
    if (!error_file.empty()) {
      int flags = O_WRONLY | O_CREAT | (is_error_append ? O_APPEND : O_TRUNC);
      int fd = open(error_file.c_str(), flags, 0644);
      if (fd == -1) {
        cerr << "Failed to open " << error_file << " for writing" << endl;
        exit(1);
      }
      dup2(fd, STDERR_FILENO);
      close(fd);
    }
    
    // Properly allocate mutable strings for execv (required by POSIX)
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

/**
 * @brief Executes a sequence of commands connected by pipes.
 *
 * For N commands, N-1 anonymous pipes are created.  Each command is run in
 * its own forked child:
 *  - The first child's stdin is inherited from the shell.
 *  - Each intermediate child reads from the previous pipe and writes to the
 *    next.
 *  - The last child writes to either the next pipe's write end or, if an
 *    output redirection was specified on the last command, to the redirect
 *    file.
 *  - All pipe file descriptors are closed in both parent and children after
 *    they have been @c dup2()'d where needed.
 *
 * Built-in commands are handled inside children via @c executeBuiltinInChild().
 * External commands are launched with @c execv().
 * The parent waits for all children before returning.
 *
 * @param[in] commands  Ordered list of @c CommandInfo objects forming the pipeline.
 */
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
    
    bool is_builtin = isBuiltin(cmd.args[0]);
    string path;
    
    if (!is_builtin) {
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
      if (i > 0) {
        dup2(pipes[i - 1][0], STDIN_FILENO);
      }
      
      if (i < num_commands - 1) {
        dup2(pipes[i][1], STDOUT_FILENO);
      }
      
      for (int j = 0; j < num_commands - 1; ++j) {
        close(pipes[j][0]);
        close(pipes[j][1]);
      }
      
      if (i == num_commands - 1 && cmd.has_redirect && !cmd.output_file.empty()) {
        int flags = O_WRONLY | O_CREAT | (cmd.is_append ? O_APPEND : O_TRUNC);
        int fd = open(cmd.output_file.c_str(), flags, 0644);
        if (fd == -1) {
          cerr << "Failed to open " << cmd.output_file << " for writing" << endl;
          exit(1);
        }
        dup2(fd, STDOUT_FILENO);
        close(fd);
      }
      
      if (cmd.has_error_redirect && !cmd.error_file.empty()) {
        int flags = O_WRONLY | O_CREAT | (cmd.is_error_append ? O_APPEND : O_TRUNC);
        int fd = open(cmd.error_file.c_str(), flags, 0644);
        if (fd == -1) {
          cerr << "Failed to open " << cmd.error_file << " for writing" << endl;
          exit(1);
        }
        dup2(fd, STDERR_FILENO);
        close(fd);
      }
      
      if (is_builtin) {
        executeBuiltinInChild(cmd.args);
      } else {
        // Properly allocate mutable strings for execv (required by POSIX)
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

/**
 * @brief Readline hook that formats and prints tab-completion candidates.
 *
 * Registered as @c rl_completion_display_matches_hook so that matches are
 * displayed on a single line separated by two spaces, without the default
 * column-aligned pager layout.  After printing, the current prompt and
 * partial input are redrawn via @c rl_on_new_line() / @c rl_redisplay().
 *
 * @param[in] matches      Null-terminated array of match strings; index 0 is
 *                          the longest common prefix (not printed), index 1..N
 *                          are the individual candidates.
 * @param[in] num_matches  Number of candidates (excluding the common prefix).
 * @param[in] (max_length) Maximum candidate string length (unused).
 */
static void display_matches_hook(char **matches, int num_matches, int /*max_length*/) {
  fprintf(rl_outstream, "\n");
  for (int i = 1; i <= num_matches; i++) {
    if (i > 1) fprintf(rl_outstream, "  ");
    fprintf(rl_outstream, "%s", matches[i]);
  }
  fprintf(rl_outstream, "\n");
  rl_on_new_line();
  rl_redisplay();
}

/**
 * @brief Shell entry point implementing the Read-Eval-Print Loop (REPL).
 *
 * Startup sequence:
 *  1. Enables immediate flushing of @c cout and @c cerr.
 *  2. Registers @c command_completion and @c display_matches_hook with readline.
 *  3. Installs @c sigchld_handler for asynchronous background-job reaping.
 *  4. Loads command history from @c HISTFILE (if set).
 *
 * REPL loop:
 *  1. **Reap** — notify the user of any completed background jobs.
 *  2. **Read** — display the `$ ` prompt and read a line via readline;
 *     exit on EOF (@c Ctrl-D).
 *  3. **Eval** — parse the line with @c parsePipeline() and dispatch:
 *       - Empty input: skip.
 *       - Pipeline (`|`): call @c executePipeline().
 *       - Background job (`&`): fork without waiting, register in @c bg_jobs.
 *       - Built-in commands: handle inline with I/O redirection scaffolding.
 *       - External commands: call @c executeProgram().
 *  4. **Restore** — undo any stdout/stderr redirection done for built-ins.
 *  5. Loop to step 1.
 *
 * Shutdown:
 *  - Writes the full readline history to @c HISTFILE (if set).
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
      redirect_fd = open(cmd_info.output_file.c_str(), flags, 0644);
      if (redirect_fd != -1) {
        dup2(redirect_fd, STDOUT_FILENO);
      }
    }
    
    if (cmd_info.has_error_redirect && !cmd_info.error_file.empty()) {
      saved_stderr = dup(STDERR_FILENO);
      int flags = O_WRONLY | O_CREAT | (cmd_info.is_error_append ? O_APPEND : O_TRUNC);
      error_redirect_fd = open(cmd_info.error_file.c_str(), flags, 0644);
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
          last_appended_index = end - 1;
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
      // Drain all exited children first
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
      // Fallback: check individual jobs (handles ECHILD if already reaped)
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
    else if (program == "cd" && args.size() > 1) {
      string path = args[1];
      
      // Expand ~ to home directory
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
      // Restore stdout and stderr before forking for external programs
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
    
    // Restore stdout and stderr for built-in commands if redirected
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
