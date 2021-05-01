#include <fcntl.h>    // open?
#include <limits.h>   // PATH_MAX
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

struct commandline *createCommandline(char *line) {
  struct commandline *currLine = malloc(sizeof(struct commandline));

  // parse input with space delimiter
  char *saveptr;
  char *token = strtok_r(line, " ", &saveptr);
  if (token == NULL) {
    return NULL;
  }

  // first token is command
  currLine->command = calloc(strlen(token) + 1, sizeof(char));
  strcpy(currLine->command, token);

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
      currLine->redirectInput = calloc(strlen(token) + 1, sizeof(char));
      strcpy(currLine->redirectInput, token);
    } else if (strcmp(token, ">") == 0) {
      // next token is output file
      token = strtok_r(NULL, " ", &saveptr);
      currLine->redirectOutput = calloc(strlen(token) + 1, sizeof(char));
      strcpy(currLine->redirectOutput, token);
    } else if (strcmp(token, "&") == 0) {
      currLine->background = true;
    } else {
      // is an arg
      currLine->args[currLine->argsCount++] = token;
    }
  }
  /*
  if (currLine->argsCount == 0) {
    free(currLine->args);
    currLine->args = calloc(2, sizeof(char *));
    strcpy(currLine->args[0], currLine->command);
  }
  */
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
    char *homeDir = getenv("HOME");
    chdir(getenv("HOME"));
    printf("homeDir:%s\n", homeDir);
    fflush(stdout);
  } else {
    char *destDir = currLine->args[0];
    printf("destDir:%s\n", destDir);
    fflush(stdout);
    // handle relative and absolute paths
    // getcwd returns absolute pathname with null term string
    // char cwd[PATH_MAX];
    char *cwd = getcwd(NULL, PATH_MAX);
    if (cwd == NULL) {
      perror("getcwd()");
      exit(1);
    }
    // if destDir includes cwd, it is absolute path
    if (strstr(destDir, cwd) != NULL) {
      chdir(destDir);
    } else {
      char absolutePath[PATH_MAX];
      sprintf(absolutePath, "%s/%s", cwd, destDir);
      chdir(absolutePath);
    }
    free(cwd);
  }
}

void printStatus(struct commandline *currLine) {
  // print either exit status or terminating signal of last foreground process
  // ran by shell exit, cd, status do not count as foreground processes (ie
  // status should ignore build-in commands)
  if (currLine->background) {
    printf("terminated by signal %d\n", lastExitStatus);
  } else {
    printf("exit value %d\n", lastExitStatus);
  }
  fflush(stdout);
}

void otherCommands(struct commandline *currLine) {
  // module exploration "process api executing a new program"
  // command without & means foreground, parent must wait for completion
  // command with & must be run as background, parent shell must not wait
  // for command to complete. parent must return command line access to use
  // immediately after forking

  // copy args to newargv for execvp
  int newargvLen = currLine->argsCount + 1;
  char *newargv[newargvLen];
  newargv[newargvLen - 1] = NULL;  // null terminate array
  newargv[0] = currLine->command;  // first is command
  for (int i = 1; i < newargvLen - 1; ++i) {
    newargv[i] = currLine->args[i];
  }

  // fork a new process
  int childStatus;
  pid_t childPid = fork();

  switch (childPid) {
    case -1:
      perror("fork()\n");
      break;
    case 0:
      // child process executes this branch
      execvp(currLine->command, newargv);
      // returns if there is an error
      perror("execvp");
      exit(2);
      break;
    default:
      // parent process executes this branch
      // wait for child's termination
      childPid = waitpid(childPid, &childStatus, 0);
      // check if child process terminated normally
      if (WIFEXITED(childStatus)) {
        lastExitStatus = WEXITSTATUS(childStatus);
        printf("Child %d exited normally with status %d\n", childPid,
               WEXITSTATUS(childStatus));
      } else {
        lastExitStatus = WTERMSIG(childStatus);
        printf("Child %d exited abnormally due to signal %d\n", childPid,
               WTERMSIG(childStatus));
      }
      fflush(stdout);
      break;
  }
  fflush(stdout);
}

int main(void) {
  char *line = malloc(MAX_CMD_LINE * sizeof(char));
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
      printf("%d\n", lastExitStatus);
      fflush(stdout);
    } else {
      // must fork
      otherCommands(currLine);
    }
    currLine = getInput(line);
  }
  // "exit" MUST KILL ALL PROCESSES AND JOBS BEFORE TERMINATING
  // exploration process api creating and terminating processes
  // exit() function
  /*// redirect >
  if (strcmp(argsArray[1], ">") == 0) {
    // open target file
    int targetFD = open(argsArray[2], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (targetFD == -1) {
      perror("target open()");
      exit(1);
    }
    printf("targetFD = %d\n", targetFD);
    close(1);
    dup(targetFD);
    execv(newargv[0], newargv);
  }
// 11
*/
  free(line);
  destroyCommandline(currLine);
  return 0;
}
