/**
 * @file jobs.h
 * @brief Background job management: reaping, tracking, and SIGCHLD handling.
 */
#pragma once

#include "globals.h"

/**
 * @brief Returns the lowest positive integer not already in use as a job number.
 */
int nextJobNumber();

/**
 * @brief SIGCHLD signal handler that non-blockingly reaps exited or signalled
 *        background children.
 */
void sigchld_handler(int);

/**
 * @brief Reaps all completed background jobs, prints their completion status
 *        to stdout, and removes them from bg_jobs.
 */
void reapJobs();
