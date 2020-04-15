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
#include <sys/stat.h>

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

/* To check if program executable */
struct stat sb;

int cmd_pwd(struct tokens *tokens);
int cmd_cd(struct tokens *tokens);
int cmd_execute(struct tokens *tokens);
int cmd_wait(unused struct tokens *tokens);
int cmd_exit(struct tokens *tokens);
int cmd_help(struct tokens *tokens);
void set_signals(__sighandler_t constant);

/* Built-in command functions take token array (see parse.h) and return int */
typedef int cmd_fun_t(struct tokens *tokens);

/* Built-in command struct and lookup table */
typedef struct fun_desc {
  cmd_fun_t *fun;
  char *cmd;
  char *doc;
} fun_desc_t;

fun_desc_t cmd_table[] = {
  {cmd_pwd, "pwd", "prints current working directory"},
  {cmd_cd, "cd", "takes one argument, a directory path, and changes the current working directory to that directory"},
  {cmd_wait, "wait", "Waits all child processes before they finish their work "},
  {cmd_help, "?", "show this help menu"},
  {cmd_exit, "exit", "exit the command shell"},
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

/* Prints current working directory */
int cmd_pwd(unused struct tokens *tokens) {
  int path_size = 1024;
  char* path = malloc(path_size * sizeof(char));

  while(getcwd(path, path_size * sizeof(char)) == NULL) {
    path_size *= 2;
    path = realloc(path, path_size * sizeof(char));
    if (path_size > 10000) break;
  }

  if (path != NULL) {
    printf("%s\n", path);
  } else {
    printf("error: getcwd error");
  }
  
  free(path);
  return 1;
}

/* Takes one argument, a directory path, and changes the current working directory to that directory */
int cmd_cd(unused struct tokens *tokens) {
  char * new_directory_path = tokens_get_token(tokens, 1);
  if (new_directory_path == NULL) {
    new_directory_path = getenv("HOME");
  }

  if (chdir(new_directory_path) != 0) {
    printf("cd: No such file or directory\n");
  } else {
    printf("%s\n", new_directory_path);
  }

  return 1;
}

/* Waits all children before they finish their work */
int cmd_wait(unused struct tokens *tokens) {
  while(wait(NULL) > 0){}
  return 1;
}

bool executable (char * program_name) {
  return (stat(program_name, &sb) == 0 && sb.st_mode & S_IXUSR);
}

void get_executable_program_name(char * program_name) {
  if (executable(program_name)) {
    return;
  }

  char* token = strtok(getenv("PATH"), ":");
  while(token) {
    char* curr_path = strdup(token);
    curr_path = strcat(curr_path, "/");
    curr_path = strcat(curr_path, program_name);
    
    if (executable(curr_path)) {
      program_name = realloc(program_name, strlen(curr_path) + 1);
      program_name = strcpy(program_name, curr_path);
      free(curr_path);
      return;
    } 
    
    token = strtok(NULL, ":");
    free(curr_path);
  }
  
}

int cmd_execute(unused struct tokens *tokens) {
  int child_pid, status;
  bool is_background_process = false;
  size_t tokens_size = tokens_get_length(tokens);
  if (strcmp(tokens_get_token(tokens, tokens_size - 1), "&") == 0) {
    tokens_size--;
    is_background_process = true;
  }

  child_pid = fork();
  if (child_pid > 0) { /* Parent Process */
    setpgid(child_pid, child_pid);
    
    if (is_background_process) {
      printf("[%d]\n", child_pid);
    } else {
      tcsetpgrp(shell_terminal, getpgid(child_pid));
      waitpid(child_pid, &status, WUNTRACED);
      tcsetpgrp(shell_terminal, getpgid(0));
    }

  } else if (child_pid == 0) { /* Child Process */
    setpgid(getpid(), getpid());
    
    char* program_name = strdup(tokens_get_token(tokens, 0));
    char** args = malloc((tokens_size + 1) * sizeof(char*));
    
    get_executable_program_name(program_name);
    args[0] = program_name;
    int args_num = 1;
    char * curr_token;
    int in_out = -1;
    bool in_open = false, out_open = false;
    for (int i = 1; i < tokens_size; i++) {
      curr_token = tokens_get_token(tokens, i);
      if (strcmp(curr_token, "<") == 0) {
        in_out = 1;
      } else if (strcmp(curr_token, ">") == 0) { 
        in_out = 0;
      } else if (in_out == 1) {
        freopen(curr_token, "r", stdin);
        in_out = -1;
        in_open = true;
      } else if (in_out == 0) {
        freopen(curr_token, "w", stdout);
        in_out = -1;
        out_open = true;
      } else {
        args[args_num++] = curr_token;
      }
    }
    args[args_num] = NULL;

    if (!is_background_process) {
      set_signals(SIG_DFL);
    }
    
    execv(program_name, args);
    printf("error: no such program or illegal arguments\n");
    free(program_name);
    free(args);

    if (in_open) fclose(stdin);
    if (out_open) fclose(stdout);
    exit(1);
  }

  return 1;
}

void set_signals(__sighandler_t constant) {
  signal(SIGINT, constant);
  signal(SIGQUIT, constant);
  signal(SIGTSTP, constant);
  signal(SIGCHLD, constant);
  signal(SIGCONT, constant);
  signal(SIGTTIN, constant);
  signal(SIGTTOU, constant);
}

/* Looks up the built-in command, if it exists. */
int lookup(char cmd[]) {
  for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    if (cmd && (strcmp(cmd_table[i].cmd, cmd) == 0))
      return i;
  return -1;
}

/* Intialization procedures for this shell */
void init_shell() {
  /* Our shell is connected to standard input. */
  shell_terminal = STDIN_FILENO;

  /* Check if we are running interactively */
  shell_is_interactive = isatty(shell_terminal);

  if (shell_is_interactive) {
    /* If the shell is not currently in the foreground, we must pause the shell until it becomes a
     * foreground process. We use SIGTTIN to pause the shell. When the shell gets moved to the
     * foreground, we'll receive a SIGCONT. */
    while (tcgetpgrp(shell_terminal) != (shell_pgid = getpgrp()))
      kill(-shell_pgid, SIGTTIN);

    /* Saves the shell's process id */
    shell_pgid = getpid();

    /* Take control of the terminal */
    tcsetpgrp(shell_terminal, shell_pgid);

    /* Save the current termios to a variable, so it can be restored later. */
    tcgetattr(shell_terminal, &shell_tmodes);
  }
}

int main(unused int argc, unused char *argv[]) {
  init_shell();

  static char line[4096];
  int line_num = 0;

  /* Please only print shell prompts when standard input is not a tty */
  if (shell_is_interactive)
    fprintf(stdout, "%d: ", line_num);

  /* Ignore signals */
  set_signals(SIG_IGN);

  while (fgets(line, 4096, stdin)) {
    /* Split our line into words. */
    struct tokens *tokens = tokenize(line);

    /* Find which built-in function to run. */
    int fundex = lookup(tokens_get_token(tokens, 0));

    if (fundex >= 0) {
      cmd_table[fundex].fun(tokens);
    } else {
      if (tokens_get_length(tokens) != 0) {
        cmd_execute(tokens);
      }
    }

    if (shell_is_interactive)
      /* Please only print shell prompts when standard input is not a tty */
      fprintf(stdout, "%d: ", ++line_num);

    /* Clean up memory */
    tokens_destroy(tokens);
  }

  return 0;
}
