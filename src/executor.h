/**
 * @file executor.h
 * @brief Command lookup, built-in execution, and program/pipeline running.
 */
#pragma once

#include "globals.h"
#include "parser.h"

#include <string>
#include <vector>

/**
 * @brief Returns true if @p cmd is a recognized shell built-in.
 */
bool isBuiltin(const std::string& cmd);

/**
 * @brief Searches PATH for an executable named @p program and returns its
 *        absolute path, or an empty string if not found.
 */
std::string findInPath(const std::string& program);

/**
 * @brief Executes a built-in command inside a forked child process, then
 *        calls exit().  Used so built-ins can participate in pipelines.
 */
void executeBuiltinInChild(const std::vector<std::string>& args);

/**
 * @brief Forks a child, optionally redirects stdout/stderr, and runs an
 *        external program via execv().  Waits for the child to exit.
 */
void executeProgram(const std::string& path,
                    const std::vector<std::string>& args,
                    const std::string& output_file = "",
                    bool is_append = false,
                    const std::string& error_file = "",
                    bool is_error_append = false);

/**
 * @brief Executes a sequence of commands connected by pipes.
 */
void executePipeline(const std::vector<CommandInfo>& commands);
