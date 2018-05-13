#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <limits.h>
#include <fcntl.h>

#include "tokenizer.h"

/* Convenience macro to silence compiler warnings about unused function parameters. */
#define unused __attribute__((unused))

/* Whether the shell is connected to an actual terminal or not. */
bool shell_is_interactive;

/* File descriptor for the shell input */
int shell_terminal;

/* Terminal mode settings for the shell */
struct termios shell_tmodes;

/* Process group id for the shell */
pid_t shell_pgid;

/* Declare functions */
int cmd_exit(struct tokens *tokens);
int cmd_help(struct tokens *tokens);
int cmd_pwd(struct tokens *tokens);
int cmd_cd(struct tokens *tokens);
int cmd_wait(struct tokens *tokens);

/* Built-in command functions take token array (see parse.h) and return int */
typedef int cmd_fun_t(struct tokens *tokens);

/* Built-in command struct and lookup table */
typedef struct fun_desc {
  cmd_fun_t *fun;
  char *cmd;
  char *doc;
} fun_desc_t;

fun_desc_t cmd_table[] = {
  {cmd_help, "?", "show this help menu"},
  {cmd_exit, "exit", "exit the command shell"},
  {cmd_pwd, "pwd", "prints the current working directory"},
  {cmd_cd, "cd", "changes the current working directory to target directory"},
  {cmd_wait, "wait", "waits until all background jobs before returning"},
};

/* Prints a helpful description for the given command */
int cmd_help(unused struct tokens *tokens) {
  for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    printf("%s - %s\n", cmd_table[i].cmd, cmd_table[i].doc);
  return 1;
}

/* Exits this shell */
int cmd_exit(unused struct tokens *tokens) {
  exit(0);
}

/* Prints the current working directory */
int cmd_pwd(unused struct tokens *tokens) {
  char buff[PATH_MAX+1];
  if (getcwd(buff, PATH_MAX)) {
    fprintf(stdout, "%s\n", buff);
    return 0;
  }
  fprintf(stderr, "Error: %s\n", strerror(errno));
  return -1;
}

/* Changes the current working directory to target directory */
int cmd_cd(struct tokens *tokens) {
  if (chdir(tokens_get_token(tokens, 1)) == -1) {
    fprintf(stderr, "cd: %s: %s\n", tokens_get_token(tokens, 1), strerror(errno));
    return -1;
  }
  return 0;
}

/* Waits until all background jobs before returning */
int cmd_wait(struct tokens *tokens) {
  int status;
  while (wait(&status) > 0);
  return 0;
}

/* Restore signals handlers so processes respond to 
 * signals with default action. 
 */
void restore_default_signal() {
  signal(SIGINT,  SIG_DFL);
  signal(SIGQUIT, SIG_DFL);
  signal(SIGTERM, SIG_DFL);
  signal(SIGTSTP, SIG_DFL);
  signal(SIGCONT, SIG_DFL);
  //signal(SIGTTIN, SIG_DFL);
  //signal(SIGTTOU, SIG_DFL);
}

/* Set the IO redirection based on redirection type. 
* - if io_redir = 1, input redirection is supported
 * - if io_redir = 0, no I/O redirection is supported
 * - if io_redir = -1, output redirection is supported
 */
void redirect_io(int io_redir, char *redirect_file_path) {
  int newfd;
  if (io_redir == 1) { // input redirect
    newfd = open(redirect_file_path, O_RDONLY, 0644);
    if (newfd < 0) {
      fprintf(stderr, "%s: No such file or directory\n", redirect_file_path);
      exit(1);
    }
  } else { // output redirect
    newfd = open(redirect_file_path, O_CREAT|O_TRUNC|O_WRONLY, 0644);
    if (newfd < 0) {
      fprintf(stderr, "%s: Cannot open or create file\n", redirect_file_path);
      exit(1);
    }
  }
  if (io_redir == 1) { // input redirect
    dup2(newfd, 0);
  } else { // output redirect
    dup2(newfd, 1);
  }
}

/* Run a execv call in a forked child process 
 * and I/O redirection to redirect_file_path.
 */
int run_execv(char *arg, char **argv, int io_redir, 
              char *redirect_file_path, int is_bg) {
  pid_t pid = fork();
  int status;
  if (pid == 0) { // child
    restore_default_signal();
    if (io_redir) 
      redirect_io(io_redir, redirect_file_path);
    setpgid(getpid(), getpid());
    if (!is_bg) {
      tcsetpgrp(shell_terminal, getpid());
    }
    if (execv(arg, argv) < 0) {
      fprintf(stderr, "This shell doesn't know how to run this program/command.\n");
    }
    //fprintf(stdout, "[%d]+ Done\n", pid);
    exit(0);
  } else if (pid > 0) { // parent
    if (!is_bg) {
      wait(&status);
      tcsetpgrp(shell_terminal, getpid());
    }
  } else { // failed to fork
    fprintf(stderr, "Failed to fork a child process to run command.\n");
    return -1;
  }
  return 0;
}

/* Return 1 if a file defined by PATH exists. Return 0 otherwise. */
int file_exists(char *path) {
  FILE *file;
  if ((file = fopen(path, "r"))) {
    fclose(file);
    return 1;
  }
  return 0;
}

/* Return the absolute path of a relative path using PATH
 * environment variable. Otherwise, the relative path is returned.
 * Note the returned value is always a new dynamically allocated variable.
 */
char* resolve_path(char *path) {
  // Get PATH environment variable and make a copy to prevent modification by strtok
  char *pPath = getenv("PATH");
  char path_env[strlen(pPath) + 1];
  strcpy(path_env, pPath);

  // Resolve absolute path
  char *path_prefix;
  char *absolute_path = (char *) malloc(PATH_MAX + 1);

  if (!file_exists(path) && pPath) {
    path_prefix = strtok(path_env, ":");
    while (path_prefix != NULL) {
      strcpy(absolute_path, path_prefix);
      strncat(absolute_path, "/", 1);
      strncat(absolute_path, path, PATH_MAX);
      if (file_exists(absolute_path)) {
        return absolute_path;
      }
      path_prefix = strtok(NULL, ":");
    }
  }
  // Still return the same path, if absolute path cannot find
  strcpy(absolute_path, path);
  return absolute_path;
}

/* Execute a program using execv with path resolution 
 * and IO redirection to redirect_file_path if io_redit != 0
 */
int execute_program(struct tokens *tokens, int io_redir, char *redirect_file_path, int is_bg) {
  size_t args_length = tokens_get_length(tokens);
  if (args_length <= 0) {
    return 0;
  }
  char *args[args_length+1];
  for (size_t i = 0; i < args_length; i++) {
    args[i] = resolve_path(tokens_get_token(tokens, i));
  }
  args[args_length] = NULL;
  int status = run_execv(args[0], args, io_redir, redirect_file_path, is_bg);
  // free absolute paths
  for (size_t i = 0; i < args_length; i++) {
    free(args[i]);
  }
  return status;
}

/* Return 1 if input redirection, -1 if output redirection, 0 if no redirection. */
int io_redirection_type(char *line) {
  if (strchr(line, '<')) {
    return 1;
  } else if (strchr(line, '>')) {
    return -1;
  } else {
    return 0;
  }
}

/* Looks up the built-in command, if it exists. */
int lookup(char cmd[]) {
  for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    if (cmd && (strcmp(cmd_table[i].cmd, cmd) == 0))
      return i;
  return -1;
}

/* Initialization procedures for this shell */
void init_shell() {
  /* Our shell is connected to standard input. */
  shell_terminal = STDIN_FILENO;

  /* Check if we are running interactively */
  shell_is_interactive = isatty(shell_terminal);

  if (shell_is_interactive) {
    /* If the shell is not currently in the foreground, we must pause the shell until it becomes a
     * foreground process. We use SIGTTIN to pause the shell. When the shell gets moved to the foreground, we'll receive a SIGCONT. */
    while (tcgetpgrp(shell_terminal) != (shell_pgid = getpgrp()))
      kill(-shell_pgid, SIGTTIN);

    /* Ignore all default signals */
    signal(SIGINT,  SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGTERM, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGCONT, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);

    /* Saves the shell's process id */
    shell_pgid = getpid();

    /* Move shell into its own process group. */
    if (setpgid(shell_pgid, shell_pgid) < 0) {
      fprintf(stderr, "Cannot put shell in its own process group.\n");
      exit(1);
    }
    // if (setpgid(shell_pgid, shell_pgid) < 0) {
    //   fprintf(stderr, "Cannot put shell in its own process group.\n");
    // }

    /* Take control of the terminal */
    tcsetpgrp(shell_terminal, shell_pgid);

    /* Save the current termios to a variable, so it can be restored later. */
    tcgetattr(shell_terminal, &shell_tmodes);
  }
}

int main(unused int argc, unused char *argv[]) {
  init_shell();

  static char line[4096];
  char *redirect_file_path;
  int line_num = 0, io_redir = 0;

  /* Please only print shell prompts when standard input is not a tty */
  if (shell_is_interactive)
    fprintf(stdout, "%d: ", line_num);

  while (fgets(line, 4096, stdin)) {

    /* Arguments passed to the process. */
    struct tokens *tokens;

    /* Check if a background processing is requested. */
    int is_bg = 0;
    if (strchr(line, '&')) {
      is_bg = 1;
    }

    /* Split our line into arguments and forwarded filename. */
    io_redir = io_redirection_type(line);
    if (io_redir) { // Has IO redirect
      tokens = tokenize(strtok(line, "<>&"));
      redirect_file_path = tokens_get_token(tokenize(strtok(NULL, "<>&")), 0);
    } else {
      if (is_bg) {
        tokens = tokenize(strtok(line, "&"));
      } else {
        tokens = tokenize(line);
      }
    }

    /* Find which built-in function to run. */
    int fundex = lookup(tokens_get_token(tokens, 0));

    if (fundex >= 0) {
        cmd_table[fundex].fun(tokens);
    } else {
        execute_program(tokens, io_redir, redirect_file_path, is_bg);
    }

    if (shell_is_interactive)
      /* Please only print shell prompts when standard input is not a tty */
      fprintf(stdout, "%d: ", ++line_num);

    /* Clean up memory */
    tokens_destroy(tokens);
  }

  return 0;
}
