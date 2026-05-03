/**
 * @file jobs.h
 * @brief Background job management: reaping, tracking, and SIGCHLD handling.
 */
#pragma once

#include "globals.h"

/**
 * @brief Returns the lowest positive integer not already in use as a job number.
 *
 * @return The next available job number (≥ 1).
 */
int nextJobNumber();

/**
 * @brief SIGCHLD handler that non-blockingly reaps exited background children
 *        and marks matching bg_jobs entries as done.  Safe for use with SA_RESTART.
 *
 * @param sig  Signal number (always SIGCHLD; not used).
 */
void sigchld_handler(int sig);

/**
 * @brief Reaps all completed background jobs, prints their completion status
 *        to stdout in `[N]+/-  Done   command` format, and removes them
 *        from bg_jobs.
 */
void reapJobs();

/**
 * @brief Implements the `jobs` builtin: performs a non-blocking reap pass,
 *        then prints every job in `[N]+/-  Status   command` format
 *        (Status is "Running" or "Done") and removes completed entries
 *        from bg_jobs.
 */
void listJobs();
