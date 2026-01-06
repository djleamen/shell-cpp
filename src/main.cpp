#include <iostream>
#include <string>
#include <sstream>
#include <filesystem>
#include <cstdlib>

using namespace std;
namespace fs = std::filesystem;

int main() {
  // Flush after every std::cout / std:cerr
  cout << unitbuf;
  cerr << unitbuf;

  // Read: Display a prompt and wait for user input
  while (true) {
    cout << "$ ";
    string command;
    getline(cin, command);

    // Eval: Parse and execute the command

    // exit
    if (command == "exit") {
      break;
    }
    // echo
    else if (command.substr(0, 4) == "echo") {
      cout << command.substr(5) << endl;
    }
    // type
    else if (command.substr(0, 5) == "type ") {
      string arg = command.substr(5);
      if (arg == "exit" || arg == "echo" || arg == "type") {
        cout << arg << " is a shell builtin" << endl;
      } else {
        // Search for executable in PATH
        const char* path_env = getenv("PATH");
        if (path_env) {
          string path_str(path_env);
          stringstream ss(path_str);
          string dir;
          bool found = false;
          
          while (getline(ss, dir, ':')) {
            fs::path full_path = fs::path(dir) / arg;
            if (fs::exists(full_path) && (fs::status(full_path).permissions() & fs::perms::owner_exec) != fs::perms::none) {
              cout << arg << " is " << full_path.string() << endl;
              found = true;
              break;
            }
          }
          
          if (!found) {
            cout << arg << ": not found" << endl;
          }
        } else {
          cout << arg << ": not found" << endl;
        }
      }
    }
    // Print: Display the output or error message
    else {
      cout << command << ": command not found" << endl;
    }

    // Loop: Return to step 1 and wait for the next command
  }
  return 0;
}
