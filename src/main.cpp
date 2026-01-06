#include <iostream>
#include <string>

using namespace std;

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
    if (command == "exit") {
      break;
    }
    else if (command.substr(0, 4) == "echo") {
      cout << command.substr(5) << endl;
    }
    else if (command.substr(0, 5) == "type ") {
      string arg = command.substr(5);
      if (arg == "exit" || arg == "echo" || arg == "type") {
        cout << arg << " is a shell builtin" << endl;
      } else {
        cout << "type: " << arg << ": not found" << endl;
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
