/**
 * @file jobs.cpp
 * @brief Implementations of background job management functions.
 */
#include "jobs.h"

#include <iostream>
#include <algorithm>
#include <sys/wait.h>
#include <cerrno>
#include <ctime>
#include <unistd.h>

using namespace std;

int nextJobNumber() {
  int num = 1;
  while (true) {
    bool used = false;
    for (const auto& job : bg_jobs()) {
      if (job.job_number == num) { used = true; break; }
    }
    if (!used) return num;
    num++;
  }
}

void sigchld_handler(int) {
  int saved_errno = errno;
  int wstatus;
  pid_t pid;
  while ((pid = waitpid(-1, &wstatus, WNOHANG)) > 0) {
    for (auto& job : bg_jobs()) {
      if (job.pid == pid && (WIFEXITED(wstatus) || WIFSIGNALED(wstatus))) {
        job.done = true;
        break;
      }
    }
  }
  errno = saved_errno;
}

void reapJobs() {
  vector<int> done_indices;
  for (int i = 0; i < (int)bg_jobs().size(); i++) {
    if (!bg_jobs()[i].done) {
      int wstatus;
      pid_t result = waitpid(bg_jobs()[i].pid, &wstatus, WNOHANG);
      if ((result > 0 && (WIFEXITED(wstatus) || WIFSIGNALED(wstatus))) ||
          (result < 0 && errno == ECHILD)) {
        bg_jobs()[i].done = true;
      }
    }
    if (bg_jobs()[i].done) {
      done_indices.push_back(i);
    }
  }
  int n = bg_jobs().size();
  for (int i = 0; i < n; i++) {
    if (std::ranges::find(done_indices, i) != done_indices.end()) {
      const auto& job = bg_jobs()[i];
      char marker = ' ';
      if (i == n - 1) marker = '+';
      else if (i == n - 2) marker = '-';
      string status_str = "Done";
      status_str.resize(24, ' ');
      string cmd = job.command;
      if (cmd.ends_with(" &")) {
        cmd = cmd.substr(0, cmd.size() - 2);
      }
      cout << "[" << job.job_number << "]" << marker << "  " << status_str << cmd << endl;
    }
  }
  for (int i = done_indices.size() - 1; i >= 0; i--) {
    bg_jobs().erase(bg_jobs().begin() + done_indices[i]);
  }
}

void listJobs() {
  {
    int wstatus;
    pid_t p;
    while ((p = waitpid(-1, &wstatus, WNOHANG)) > 0) {
      for (auto& job : bg_jobs()) {
        if (job.pid == p && (WIFEXITED(wstatus) || WIFSIGNALED(wstatus))) {
          job.done = true;
          break;
        }
      }
    }
  }
  for (auto& job : bg_jobs()) {
    if (!job.done) {
      int wstatus;
      // Retry briefly to handle race where the process has exited but not yet
      // become a zombie (e.g., just received EOF from a FIFO write-end close).
      for (int attempt = 0; attempt < 5; attempt++) {
        pid_t result = job.done ? 0 : waitpid(job.pid, &wstatus, WNOHANG);
        bool exited = (result > 0 && (WIFEXITED(wstatus) || WIFSIGNALED(wstatus)));
        bool no_child = (result < 0 && errno == ECHILD);
        if (job.done || exited || no_child) {
          job.done = true;
          break;
        }
        if (attempt < 4) {
          struct timespec ts = {0, 50000000}; // 50ms between retries
          nanosleep(&ts, nullptr);
        }
      }
    }
  }
  vector<int> done_indices;
  for (int i = 0; i < (int)bg_jobs().size(); i++) {
    if (bg_jobs()[i].done) done_indices.push_back(i);
  }
  int n = bg_jobs().size();
  for (int i = 0; i < n; i++) {
    const auto& job = bg_jobs()[i];
    char marker = ' ';
    if (i == n - 1) marker = '+';
    else if (i == n - 2) marker = '-';
    bool is_done = std::ranges::find(done_indices, i) != done_indices.end();
    string status_str = is_done ? "Done" : "Running";
    status_str.resize(24, ' ');
    string cmd = job.command;
    if (is_done && cmd.ends_with(" &")) {
      cmd = cmd.substr(0, cmd.size() - 2);
    }
    cout << "[" << job.job_number << "]" << marker << "  " << status_str << cmd << endl;
  }
  for (int i = done_indices.size() - 1; i >= 0; i--) {
    bg_jobs().erase(bg_jobs().begin() + done_indices[i]);
  }
}
