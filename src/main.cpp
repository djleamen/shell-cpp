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
  
    // Print: Display the output or error message
    cout << command << ": command not found" << endl;

    // Loop: Return to step 1 and wait for the next command
  }
  return 0;
}
