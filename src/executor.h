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
 *
 * @param[in] cmd  The command name to check.
 * @return         true if @p cmd names a built-in, false otherwise.
 */
bool isBuiltin(const std::string& cmd);

/**
 * @brief Searches each directory in PATH for an executable named @p program.
 *
 * @param[in] program  Bare executable name to locate.
 * @return             Absolute path to the first matching executable, or an
 *                     empty string if none is found.
 */
std::string findInPath(const std::string& program);

/**
 * @brief Executes a built-in command inside a forked child process, then
 *        calls exit().  Used so built-ins can participate in pipelines.
 *
 * @param[in] args  Tokenized command words; args[0] is the built-in name.
 */
void executeBuiltinInChild(const std::vector<std::string>& args);

/**
 * @brief Forks a child, optionally redirects stdout/stderr, and runs an
 *        external program via execv().  Waits for the child to exit.
 *
 * @param[in] path             Absolute path to the executable.
 * @param[in] args             Argument list; args[0] is the program name.
 * @param[in] output_file      Redirect stdout to this path; empty = no redirect.
 * @param[in] is_append        If true, open @p output_file in append mode.
 * @param[in] error_file       Redirect stderr to this path; empty = no redirect.
 * @param[in] is_error_append  If true, open @p error_file in append mode.
 */
void executeProgram(const std::string& path,
                    const std::vector<std::string>& args,
                    const std::string& output_file = "",
                    bool is_append = false,
                    const std::string& error_file = "",
                    bool is_error_append = false);

/**
 * @brief Executes a sequence of commands connected by pipes.
 *
 * @param[in] commands  Ordered list of commands to connect via pipes.
 *                      The last command's stdout/stderr redirects are honoured.
 */
void executePipeline(const std::vector<CommandInfo>& commands);

/**
 * @brief Saves current stdout/stderr and redirects them per @p cmd's
 *        redirection fields.  Pass the same four variables to
 *        restoreBuiltinRedirects() afterwards.
 *
 * @param[in]  cmd               CommandInfo whose redirection fields are applied.
 * @param[out] saved_stdout      Saved stdout fd, or -1 if no stdout redirect.
 * @param[out] redirect_fd       Opened output file fd, or -1 if unused.
 * @param[out] saved_stderr      Saved stderr fd, or -1 if no stderr redirect.
 * @param[out] error_redirect_fd Opened error file fd, or -1 if unused.
 */
void setupBuiltinRedirects(const CommandInfo& cmd,
                           int& saved_stdout, int& redirect_fd,
                           int& saved_stderr, int& error_redirect_fd);

/**
 * @brief Restores stdout/stderr from saved file descriptors and closes
 *        them.  Sets all four variables back to -1 after restoring.
 *
 * @param[in,out] saved_stdout      Saved stdout fd; reset to -1 on return.
 * @param[in,out] redirect_fd       Opened output file fd; closed and reset to -1.
 * @param[in,out] saved_stderr      Saved stderr fd; reset to -1 on return.
 * @param[in,out] error_redirect_fd Opened error file fd; closed and reset to -1.
 */
void restoreBuiltinRedirects(int& saved_stdout, int& redirect_fd,
                             int& saved_stderr, int& error_redirect_fd);
