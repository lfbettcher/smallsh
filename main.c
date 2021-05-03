#include <fcntl.h>   // open?
#include <limits.h>  // PATH_MAX
#include <signal.h>
#include <stdbool.h>  // bool
#include <stdio.h>    // printf, fflush
#include <stdlib.h>   // exit
#include <string.h>
#include <sys/types.h>  // pid_t
#include <sys/wait.h>   // wait process
#include <unistd.h>     // execv, fork, getppid, chdir, getcwd

#define MAX_CMD_LINE 2048
#define MAX_ARGS 512

struct commandline {
  char *command;
  char **args;
  int argsCount;
  char *redirectInput;
  char *redirectOutput;
  bool background;
};

// struct commandline *currLine;
int lastExitStatus = 0;
int statusOrSignal = 0;
int smallshPid;

// functions declarations
struct commandline *getInput(char *);
struct commandline *createCommandline(char *);
void destroyCommandline(struct commandline *);
void changeDirectory(struct commandline *);
void printExitStatus();
void otherCommands(struct commandline *);

void handleCtrlC(int signal) {
  char msg[32];
  sprintf(msg, "terminated by signal %d\n", signal);
  write(STDOUT_FILENO, msg, 32);
}

void childSignalHandler() {
  // get and display exit status for child
  int childStatus;
  pid_t childPid = waitpid(-1, &childStatus, WNOHANG);
  if (childPid > 0) {
    char msg[64];
    memset(msg, '\0', 64);
    if (WIFEXITED(childStatus)) {
      sprintf(msg, "background pid %d is done: exit value %d\n", childPid,
              WEXITSTATUS(childStatus));
      write(STDOUT_FILENO, msg, 64);
    } else {
      sprintf(msg, "background pid %d is done: terminated by signal %d\n",
              childPid, WTERMSIG(childStatus));
      write(STDOUT_FILENO, msg, 64);
    }
  }
}

int main(void) {
  smallshPid = getpid();
  char *line = malloc(MAX_CMD_LINE * sizeof(char));

  // signals
  struct sigaction sa;
  // sa.sa_handler = handler;
  // sigaction(SIGINT, &sa, NULL);

  // handle ctrl+c
  signal(SIGINT, handleCtrlC);
  // get input
  struct commandline *currLine = getInput(line);
  // command
  while (currLine == NULL || strcmp(currLine->command, "exit") != 0) {
    if (currLine == NULL || strcmp(currLine->command, "#") == 0) {
      // blank or # comment, get input again
      currLine = getInput(line);
    } else if (strcmp(currLine->command, "cd") == 0) {
      // cd
      changeDirectory(currLine);
    } else if (strcmp(currLine->command, "status") == 0) {
      // status
      printExitStatus();
    } else {
      // must fork
      otherCommands(currLine);
    }
    currLine = getInput(line);
  }
  // "exit" MUST KILL ALL PROCESSES AND JOBS BEFORE TERMINATING
  // exploration process api creating and terminating processes
  // exit() function
  // 30
  free(line);
  destroyCommandline(currLine);
  return 0;
}

struct commandline *createCommandline(char *line) {
  struct commandline *currLine = malloc(sizeof(struct commandline));

  // parse input with space delimiter
  char *saveptr;
  char *token = strtok_r(line, " ", &saveptr);
  if (token == NULL) {
    return NULL;
  }

  // first token is command
  currLine->command = strdup(token);
  // currLine->command = calloc(strlen(token) + 1, sizeof(char));
  // strcpy(currLine->command, token);

  // initialize struct parts
  currLine->args = calloc(MAX_ARGS, sizeof(char *));
  currLine->argsCount = 0;
  currLine->redirectInput = NULL;
  currLine->redirectOutput = NULL;
  currLine->background = false;

  while ((token = strtok_r(NULL, " ", &saveptr))) {
    if (strcmp(token, "<") == 0) {
      // next token is input file
      token = strtok_r(NULL, " ", &saveptr);
      currLine->redirectInput = strdup(token);
      // currLine->redirectInput = calloc(strlen(token) + 1, sizeof(char));
      // strcpy(currLine->redirectInput, token);
    } else if (strcmp(token, ">") == 0) {
      // next token is output file
      token = strtok_r(NULL, " ", &saveptr);
      currLine->redirectOutput = strdup(token);
      // currLine->redirectOutput = calloc(strlen(token) + 1, sizeof(char));
      // strcpy(currLine->redirectOutput, token);
    } else {
      // is an arg
      if (strstr(token, "$$") != NULL) {
        // variable expansion $$
        char *remainingStr = token;
        // turn smallshPid into a string
        char smallshPidStr[16];
        sprintf(smallshPidStr, "%d", smallshPid);
        // set up space for new expanded string
        char expanded[MAX_CMD_LINE];
        memset(expanded, '\0', MAX_CMD_LINE * sizeof(char));
        int remainingLen = strlen(remainingStr);
        char nextTwo[3];
        char nextLetter[2];
        while (remainingStr != NULL && remainingLen > 0) {
          if (remainingLen >= 2) {
            // get next two letters
            memset(nextTwo, '\0', 3 * sizeof(char));
            strncpy(nextTwo, remainingStr, 2);
            if (strcmp(nextTwo, "$$") == 0) {
              // next two letters are $$
              strcat(expanded, smallshPidStr);  // append smallshPid
              remainingStr = &remainingStr[2];  // set pointer to after $$
              remainingLen = strlen(remainingStr);
              continue;
            }
          }
          // next two letters are not $$
          if (remainingLen >= 1) {
            // get next letter
            memset(nextLetter, '\0', 2 * sizeof(char));
            strncpy(nextLetter, remainingStr, 1);
            strcat(expanded, nextLetter);     // append next 1 letter
            remainingStr = &remainingStr[1];  // advance pointer
            remainingLen = strlen(remainingStr);
          }
        }
        currLine->args[currLine->argsCount++] = strdup(expanded);
      } else {
        currLine->args[currLine->argsCount++] = strdup(token);
      }
    }
  }

  // check last arg for &: store as boolean and remove from args
  if (currLine->argsCount > 0 &&
      strcmp(currLine->args[currLine->argsCount - 1], "&") == 0) {
    currLine->background = true;
    currLine->args[currLine->argsCount - 1] = '\0';
    --currLine->argsCount;
  }
  return currLine;
}

void destroyCommandline(struct commandline *currLine) {
  free(currLine->command);
  free(currLine->args);
  free(currLine->redirectInput);
  free(currLine->redirectOutput);
  free(currLine);
}

struct commandline *getInput(char *line) {
  // read input into line
  memset(line, '\0', MAX_CMD_LINE * sizeof(char));
  // line[0] = '\0';
  // line[sizeof(line) - 1] = ~'\0';
  printf(": ");
  fflush(stdout);
  char *input = NULL;
  input = fgets(line, MAX_CMD_LINE, stdin);
  /*
   input = gets(line);
   size_t len = MAX_CMD_LINE;
   if (getline(&line, &len, stdin) == -1) {
     printf("blank line\n");
   }
   ssize_t lineSize = getline(&line, &len, stdin);
   */
  if (input == NULL) {
    printf("null input");
    fflush(stdout);
    return NULL;
  }
  line[strcspn(line, "\n")] = 0;  // remove \n from end of input
  return createCommandline(line);
}

void changeDirectory(struct commandline *currLine) {
  if (currLine->args == NULL || currLine->argsCount == 0) {
    // home directory
    chdir(getenv("HOME"));
    fflush(stdout);
  } else {
    char *destDir = currLine->args[0];
    char *cwd = getcwd(NULL, PATH_MAX);
    if (cwd == NULL) {
      perror("getcwd()");
      exit(1);
    }
    chdir(destDir);
    free(cwd);
  }
}

void printExitStatus() {
  // print either exit status or terminating signal of last foreground
  // process ran by shell exit, cd, status do not count as foreground
  // processes (ie status should ignore build-in commands)
  if (statusOrSignal == 0) {
    printf("exit value %d\n", lastExitStatus);  // status
  } else {
    printf("terminated by signal %d\n", lastExitStatus);  // signal
  }
  fflush(stdout);
}

void backgroundPidExit(int *childStatus) {
  pid_t childPid = 0;
  while (childPid <= 0) {
    childPid = waitpid(-1, childStatus, WNOHANG);
  }

  char msg[64];
  memset(msg, '\0', 64);
  if (WIFEXITED(childStatus)) {
    sprintf(msg, "background pid %d is done: exit value %d\n", childPid,
            WEXITSTATUS(childStatus));
    write(STDOUT_FILENO, msg, 64);
  } else {
    sprintf(msg, "background pid %d is done: terminated by signal %d\n",
            childPid, WTERMSIG(&childStatus));
    write(STDOUT_FILENO, msg, 64);
  }
}

void otherCommands(struct commandline *currLine) {
  // module exploration "process api executing a new program"
  // command without & means foreground, parent must wait for completion
  // command with & must be run as background, parent shell must not wait
  // for command to complete. parent must return command line access to use
  // immediately after forking

  // copy args to newargv for execvp: +1 for command and +1 for NULL ptr
  // term
  int newargvLen = currLine->argsCount + 2;
  char *newargv[newargvLen];
  newargv[0] = currLine->command;  // first is command
  newargv[newargvLen - 1] = NULL;  // terminated by NULL pointer
  for (int i = 1; i < newargvLen - 1; ++i) {
    newargv[i] = currLine->args[i - 1];
  }

  // fork a new process
  if (currLine->background) {
    // let parent process know when child process finishes
    signal(SIGCHLD, childSignalHandler);
  }
  /* else {
     signal(SIGCHLD, SIG_IGN);
   }*/

  pid_t childPid = fork();
  int childStatus;

  switch (childPid) {
    case -1:
      perror("fork()\n");
      break;
    case 0:
      // child process executes this branch
      // dup2 stuff canvas module 5 exploration "Processes and I/O"
      if (currLine->background) {
        // background without input or output should redirect to "/dev/null"
        int devNull = open("/dev/null", O_WRONLY);
        if (currLine->redirectInput == NULL) {
          dup2(devNull, STDIN_FILENO);
        }
        if (currLine->redirectOutput == NULL) {
          dup2(devNull, STDOUT_FILENO);
        }
      }
      if (currLine->redirectInput) {
        // open source file
        int sourceFD = open(currLine->redirectInput, O_RDONLY);
        if (sourceFD == -1) {
          printf("cannot open %s for input\n", currLine->redirectInput);
          fflush(stdout);
          exit(1);
        }
        // redirect FD 0 (stdin) to source file
        int result = dup2(sourceFD, STDIN_FILENO);
        if (result == -1) {
          perror("source dup2()");
          printf("dup2() cannot redirect stdin to source file %s\n",
                 currLine->redirectInput);
          fflush(stdout);
          exit(1);
        }
      }
      if (currLine->redirectOutput) {
        // output redirection
        int targetFD =
            open(currLine->redirectOutput, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        // 0640 or 0644?
        if (targetFD == -1) {
          printf("cannot open %s for output\n", currLine->redirectOutput);
          fflush(stdout);
          exit(1);
        }
        // redirect FD 1 (stdout) to targetFD
        int result = dup2(targetFD, 1);
        if (result == -1) {
          perror("dup2");
          printf("dup2() cannot redirect stdout to target file %s\n",
                 currLine->redirectOutput);
          fflush(stdout);
          exit(1);
        }
      }
      execvp(currLine->command, newargv);
      // returns if there is an error
      // command fails because shell could not find the command to run
      // set exit status to 1
      printf("%s: no such file or directory\n", currLine->command);
      fflush(stdout);
      exit(1);
      break;
    default:
      // parent process executes this branch
      if (currLine->background == false) {
        // wait for child's termination
        childPid = waitpid(childPid, &childStatus, 0);
        // check if child process terminated normally
        if (WIFEXITED(childStatus)) {
          lastExitStatus = WEXITSTATUS(childStatus);
          statusOrSignal = 0;
        } else {
          lastExitStatus = WTERMSIG(childStatus);
          statusOrSignal = 1;
        }
      } else {
        // background process with WNOHANG
        // childPid = waitpid(childPid, &childStatus, WNOHANG);
        waitpid(childPid, &childStatus, WNOHANG);
        printf("background pid is %d\n", childPid);
        fflush(stdout);
      }

      // print background exit info if the returning pid is one of the stored
      // background pids
      // backgroundPidExit(&childStatus);
      break;
  }
}
