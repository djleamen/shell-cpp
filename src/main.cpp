/*
Shell - A simple command-line shell implementation in C++
From CodeCrafters.io build-your-own-shell (C++23)
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

using namespace std;
namespace fs = std::filesystem;

int last_appended_index = -1;

const char* builtin_commands[] = {
  "echo",
  "exit",
  "type",
  "pwd",
  "cd",
  "history",
  nullptr
};

char* command_generator(const char* text, int state) {
  static int list_index, len;
  static vector<string> path_executables;
  static size_t path_exec_index;
  const char* name;
  
  if (!state) {
    list_index = 0;
    len = strlen(text);
    path_executables.clear();
    path_exec_index = 0;
    
    const char* path_env = getenv("PATH");
    if (path_env) {
      string path_str(path_env);
      stringstream ss(path_str);
      string dir;
      
      while (getline(ss, dir, ':')) {
        if (!fs::exists(dir)) continue;
        
        try {
          for (const auto& entry : fs::directory_iterator(dir)) {
            if (entry.is_regular_file()) {
              string filename = entry.path().filename().string();
              if (filename.length() >= len && filename.substr(0, len) == text) {
                // Check if executable
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
  
  // Return builtin commands
  while ((name = builtin_commands[list_index++])) {
    if (strncmp(name, text, len) == 0) {
      return strdup(name);
    }
  }
  
  // Return PATH executables
  if (path_exec_index < path_executables.size()) {
    return strdup(path_executables[path_exec_index++].c_str());
  }
  
  return nullptr;
}

char** command_completion(const char* text, int start, int end) {
  // Only complete if we're at the beginning of the line
  if (start == 0) {
    return rl_completion_matches(text, command_generator);
  }
  
  // Don't attempt filename completion for arguments
  rl_attempted_completion_over = 1;
  return nullptr;
}

struct CommandInfo {
  vector<string> args;
  string output_file;
  bool has_redirect;
  bool is_append;
  string error_file;
  bool has_error_redirect;
  bool is_error_append;
};

struct PipelineInfo {
  vector<CommandInfo> commands;
  bool has_pipe;
};

// Parse command
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
      
      // Handle backslash escaping outside quotes
      if (c == '\\' && !in_single_quotes && !in_double_quotes) {
        if (i + 1 < cmd_str.length()) {
          ++i;
          current_arg += cmd_str[i]; 
        }
        else {
          current_arg += c;
        }
      }
      // Handle backslash escaping inside double quotes
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
        // Single quote toggles (only if not in double quotes)
        in_single_quotes = !in_single_quotes;
      } else if (c == '"' && !in_single_quotes) {
        // Double quote toggles (only if not in single quotes)
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
    
    // Check for output redirection (>>, 1>>, >, or 1>) and error redirection (2>)
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

bool isBuiltin(const string& cmd) {
  return cmd == "exit" || cmd == "echo" || cmd == "type" || cmd == "pwd" || cmd == "cd" || cmd == "history";
}

// Execute built-in command (for use in child process during pipeline)
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
      const char* home = getenv("HOME");
      if (home) {
        if (path == "~") {
          path = home;
        } else {
          path = string(home) + path.substr(1);
        }
      }
    }
    
    if (chdir(path.c_str()) != 0) {
      cout << "cd: " << path << ": No such file or directory" << endl;
    }
  }
  else if (program == "history") {
    if (args.size() > 2 && args[1] == "-r") {
      // history -r <file> - read history from file
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
      // history -w <file> - write history to file
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
      // history -a <file> - append new commands to file
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

// Find executable in PATH
string findInPath(const string& program) {
  const char* path_env = getenv("PATH");
  if (!path_env) return "";
  
  string path_str(path_env);
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

// Execute external program
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
    
    vector<char*> argv;
    for (const auto& arg : args) {
      argv.push_back(const_cast<char*>(arg.c_str()));
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

// Execute pipeline
void executePipeline(const vector<CommandInfo>& commands) {
  if (commands.empty()) return;
  
  int num_commands = commands.size();
  vector<pid_t> pids;
  
  // Create pipes: we need (num_commands - 1) pipes
  
  // Each pipe is an array of 2 ints: [read_fd, write_fd]
  vector<vector<int>> pipes(num_commands - 1, vector<int>(2));
  for (int i = 0; i < num_commands - 1; ++i) {
    if (pipe(pipes[i].data()) == -1) {
      cerr << "Pipe creation failed" << endl;
      return;
    }
  }
  
  // Execute each command in the pipeline
  for (int i = 0; i < num_commands; ++i) {
    const CommandInfo& cmd = commands[i];
    
    // Check if command is a built-in
    bool is_builtin = isBuiltin(cmd.args[0]);
    string path;
    
    // Find executable path for external commands
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
      // Set up stdin: read from previous pipe (except for first command)
      if (i > 0) {
        dup2(pipes[i - 1][0], STDIN_FILENO);
      }
      
      // Set up stdout: write to next pipe (except for last command)
      if (i < num_commands - 1) {
        dup2(pipes[i][1], STDOUT_FILENO);
      }
      
      // Close all pipe fds in child
      for (int j = 0; j < num_commands - 1; ++j) {
        close(pipes[j][0]);
        close(pipes[j][1]);
      }
      
      // Handle output redirection for last command
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
      
      // Handle error redirection
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
      
      // Execute command (built-in or external)
      if (is_builtin) {
        executeBuiltinInChild(cmd.args);
      } else {
        vector<char*> argv;
        for (const auto& arg : cmd.args) {
          argv.push_back(const_cast<char*>(arg.c_str()));
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
  
  // Close all pipes in parent
  for (int i = 0; i < num_commands - 1; ++i) {
    close(pipes[i][0]);
    close(pipes[i][1]);
  }
  
  // Wait for all child processes
  for (pid_t pid : pids) {
    int status;
    waitpid(pid, &status, 0);
  }
}

int main() {
  // Flush after every std::cout / std:cerr
  cout << unitbuf;
  cerr << unitbuf;

  rl_attempted_completion_function = command_completion;

  // Read: Display a prompt and wait for user input
  while (true) {
    char* input = readline("$ ");
    
    // Check for EOF (Ctrl+D)
    if (!input) {
      break;
    }
    
    string command(input);
    free(input);
    
    // Add non-empty commands to history
    if (!command.empty()) {
      add_history(command.c_str());
    }

    // Eval: Parse and execute the command
    PipelineInfo pipeline = parsePipeline(command);
    
    if (pipeline.commands.empty() || 
        (pipeline.commands.size() == 1 && pipeline.commands[0].args.empty())) {
      continue;
    }
    
    if (pipeline.has_pipe && pipeline.commands.size() > 1) {
      executePipeline(pipeline.commands);
      continue;
    }
    
    // Handle single command
    CommandInfo cmd_info = pipeline.commands[0];
    vector<string>& args = cmd_info.args;
    string program = args[0];
    
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

    // exit
    if (program == "exit") {
      break;
    }
    // echo
    else if (program == "echo") {
      for (size_t i = 1; i < args.size(); ++i) {
        if (i > 1) cout << " ";
        cout << args[i];
      }
      cout << endl;
    }
    // type
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
    // pwd
    else if (program == "pwd") {
      char cwd[1024];
      if (getcwd(cwd, sizeof(cwd)) != nullptr) {
        cout << cwd << endl;
      } else {
        cerr << "pwd: error getting current directory" << endl;
      }
    }
    // history
    else if (program == "history") {
      if (args.size() > 2 && args[1] == "-r") {
        // history -r <file>
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
        // history -a <file>
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
        // history -w <file>
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
    // cd
    else if (program == "cd" && args.size() > 1) {
      string path = args[1];
      
      // Expand ~ to home directory
      if (path == "~" || path.substr(0, 2) == "~/") {
        const char* home = getenv("HOME");
        if (home) {
          if (path == "~") {
            path = home;
          } else {
            path = string(home) + path.substr(1);
          }
        }
      }
      
      if (chdir(path.c_str()) != 0) {
        cout << "cd: " << path << ": No such file or directory" << endl;
      }
    }
    // Try to execute as external program
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

    // Loop: Return to step 1 and wait for the next command
  }
  return 0;
}
