// Smallsh Shell

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <err.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <ctype.h>
#include <stdint.h>
#ifndef UINT8_MAX
#error "No support for uint8_t"
#endif
#include "string_rep.h"

// Function to check if a string is an integer
int is_int(char* stringToCheck){
   for (int i = 0; i < strlen(stringToCheck); i++) {
        // If a non-digit is found, the string is not an integer
        if (!isdigit(stringToCheck[i])) {
            return 0;
        }
    }
   return 1;
}

// Function that resets all signals to their original disposition
void reset_signals() {
  struct sigaction oldact;

  // Loop through all possible signals and retrieve their original signal handlers
  for (int i = 1; i < NSIG; i++) {
    if (sigaction(i, NULL, &oldact) == 0) {
      // If the signal has a non-default handler, set it as the new handler
      if (oldact.sa_handler != SIG_DFL && oldact.sa_handler != SIG_IGN) {
        sigaction(i, &oldact, NULL);
      }
    }
  }
}

// SIGINT handler
void sigint_handler(int sig){
  // Do nothing
}

int main (){
  // Shell variables for $? and $!
  char fg_exit_status[10];
  fg_exit_status[0] = '\0';
  char bg_pid[10];
  bg_pid[0] = '\0';

  // Initializing variables for line and PS1
  char *line = NULL;
  char *PS1Val;
  if (getenv("PS1") != NULL){
    PS1Val = getenv("PS1");
  }
  else{
    PS1Val = "";
  }
  size_t n = 0;

  signal(SIGINT, sigint_handler);
  signal(SIGTSTP, SIG_IGN);
  
beginFor:
  for (;;) {
    // Global variables
    int background = 0;
    int infile = 0;
    int outfile = 0;
    int infileIndex;
    int outfileIndex;

    // Resetting errno
    errno = 0;

    // Checking for any unwaited for background processes
    pid_t pid1;
    int status;
    while ((pid1 = waitpid(-1, &status, WNOHANG|WUNTRACED|WCONTINUED)) > 0) {
      if (WIFEXITED(status)) {
        fprintf(stderr, "Child process %jd done. Exit status %d.\n", (intmax_t)pid1, WEXITSTATUS(status));
      } 
      else if (WIFSIGNALED(status)) {
        fprintf(stderr, "Child process %jd done. Signaled %d.\n", (intmax_t)pid1, WTERMSIG(status));
      } 
      else if (WIFSTOPPED(status)) {
        kill(pid1, SIGCONT);
        fprintf(stderr, "Child process %jd stopped. Continuing.\n", (intmax_t)pid1);
      }
    }

    // Command line prompt
    fprintf(stderr, "%s", PS1Val);

    // Resetting errno
    errno = 0;

    // Getting line from file
    ssize_t line_length = getline(&line, &n, stdin);
    if (errno != 0){  
      err(errno, "failed");
      goto beginFor;
    }
    
    int tokenCounter = 0; 

    // If no input
    if (line_length <= 1){
      goto beginFor;
    }
    // If the user inputted something 
    else if (line_length > 1){
      char* token;
      // Finding the IFS variable
      if (getenv("IFS") == NULL){
        token = strtok(line, " \t\n");
      }
      else{
        token = strtok(line, getenv("IFS"));
      }
      
      // Allocating char** array
      char** dupArray = malloc(sizeof(char*) * line_length + 1);
      while (token != NULL){
        // Checking for comments
        if (token[0] == '#'){
          break;
        }
        // Allocating each char* in the char** array as needed and copying the current token into it
        dupArray[tokenCounter] = malloc(sizeof(char) * strlen(token) + 1);
        strcpy(dupArray[tokenCounter], token);
        // Getting the next token and incrementing the counter
        if (getenv("IFS") == NULL){
          token = strtok(NULL, " \t\n");
        }
        else{
          token = strtok(NULL, getenv("IFS"));
        }
        tokenCounter++;
      }      
      // Setting the last index to NULL for later processing
      dupArray[tokenCounter] = NULL;


      // Expansion
      for (int i = 0; i < tokenCounter; i++){
        if (dupArray[i][0] == '~' && dupArray[i][1] == '/'){
          // Replaing ~/ with HOME
          string_rep(&dupArray[i], "~", getenv("HOME"));
        }
        if (strstr(dupArray[i], "$$")){
          // Replacing $$ withe the pid of the smallsh process
          char pid_str[16];
          sprintf(pid_str, "%d", getpid());
          string_rep(&dupArray[i], "$$", pid_str);
        }
        if (strstr(dupArray[i], "$?")){
          // Replacing $? with the exit status of the last foreground command
          if (fg_exit_status[0] == '\0'){
              string_rep(&dupArray[i], "$?", "0");
          }
          else {
            string_rep(&dupArray[i], "$?", fg_exit_status);
          }
        }
        if (strstr(dupArray[i], "$!")){
          // Replacing $! with the pid of the most recent background process
          if (bg_pid[0] == '\0'){
            string_rep(&dupArray[i], "$!", "");
          }
          else{
            string_rep(&dupArray[i], "$!", bg_pid);
          }
        }
      }
      // Parsing
      // If there are no tokens then reprompt user
      if (tokenCounter == 0){
        goto beginFor;
      }

      // Checking for &
      if (strcmp(dupArray[tokenCounter - 1], "&") == 0){
        background = 1;
      }

      // If the command is to be executed in the background
      if (background == 1 && tokenCounter > 2) {
        // If an infile is present
        if (strcmp(dupArray[tokenCounter - 3], "<") == 0){
          infile = 1;
          infileIndex = tokenCounter - 2;

          // If an outfile is present
          if (strcmp(dupArray[tokenCounter - 5], ">") == 0){
            outfile = 1;
            outfileIndex = tokenCounter - 4;
          }
        }

        // If an outfile is present
        else if (strcmp(dupArray[tokenCounter - 3], ">") == 0){
          outfile = 1;
          outfileIndex = tokenCounter - 2;

          // If an infile is present
          if (strcmp(dupArray[tokenCounter - 5], "<") == 0){
            infile = 1;
            infileIndex = tokenCounter - 4;
          }
        }
      }
      // If the command isn't to be executed in the background
      else if (background == 0 && tokenCounter > 2) {
        // If an infile is present
        if (strcmp(dupArray[tokenCounter - 2], "<") == 0){
          infile = 1;
          infileIndex = tokenCounter - 1;

          // If an outfile is present
          if (tokenCounter > 4 && strcmp(dupArray[tokenCounter - 4], ">") == 0){
            outfile = 1;
            outfileIndex = tokenCounter - 3;
          }
        }

        // If an outfile is present
        else if (strcmp(dupArray[tokenCounter - 2], ">") == 0){
          outfile = 1;
          outfileIndex = tokenCounter - 1;

          // If an infile is present
          if (tokenCounter > 4 && strcmp(dupArray[tokenCounter - 4], "<") == 0){
            infile = 1;
            infileIndex = tokenCounter - 3;
          }
        }
      }
      /*............
       CD BUILT-IN
     ............*/

      if (strcmp(dupArray[0], "cd") == 0){

        //Check if there are more than two arguments
        if (tokenCounter > 2){
          //Error here
          fprintf(stderr, "smallsh: cd: too many arguments\n");
        }
        else if (tokenCounter == 2){
          // Move to given directory
          if (chdir(dupArray[1]) == -1){
            fprintf(stderr, "smallsh: cd: %s: No such file or directory\n", dupArray[1]);
          }
        }
        // Move to HOME directory
        else{
          chdir(getenv("HOME"));
        }
        goto free;
      }

      /*............
       EXIT BUILT-IN
       ...........*/
      else if (strcmp(dupArray[0], "exit") == 0){
        // Get the current $? shell variable
        int exit_status = atoi(fg_exit_status);
        if (tokenCounter > 2){
          // Error here
          fprintf(stderr, "smallsh: exit: too many arguments\n");
          goto free;
        }
        // If the provided argument isn't an integer
        else if (tokenCounter == 2 && is_int(dupArray[1]) == 0){
          fprintf(stderr, "smallsh: exit: %s: numeric argument required\n", dupArray[1]);
          goto free;

        }
        else if (tokenCounter == 2){
          // Exit with provided status
          exit_status = atoi(dupArray[1]); 
        }

        else {
          // Exit with $?
          exit_status = atoi(fg_exit_status);
        }

        // Freeing dupArray
        for(int j = 0; j < tokenCounter; j++){
          free(dupArray[j]);
        }
        free(dupArray);
        free(line);
      
        fprintf(stderr, "\nexit\n");
        exit(exit_status);
      }
      
      /*...........
       USING EXEC (NON-BUILT-IN COMMANDS)
      ...........*/
      else{
        if (background == 1){
          // Freeing up the '&' character from the dupArray if it is present
          free(dupArray[tokenCounter - 1]); 
          dupArray[tokenCounter - 1] = NULL;
          tokenCounter--;
        }

        // Output redirection
        int output_fd = STDOUT_FILENO;

        if (outfile == 1){
          output_fd = open(dupArray[outfileIndex], O_WRONLY | O_CREAT, 0777); 
          if (output_fd == -1){
            fprintf(stderr, "smallsh: %s: Error opening file\n", dupArray[outfileIndex]);
            goto free;
          }
          // Freeing up the ">" token if present
          free(dupArray[outfileIndex - 1]);
          dupArray[outfileIndex - 1] = NULL;
          tokenCounter = tokenCounter - 2;
        }
        else {
          output_fd = STDOUT_FILENO;
        }

        // Input redirection
        int input_fd = STDIN_FILENO;

        if (infile == 1){
          input_fd = open(dupArray[infileIndex], O_RDONLY);
          if (input_fd == -1){
            fprintf(stderr, "smallsh: %s: No such file or directory\n", dupArray[infileIndex]);
            goto free;
          }
          // Freeing up the "<" token if present
          free(dupArray[infileIndex - 1]);
          dupArray[infileIndex - 1] = NULL;
          tokenCounter = tokenCounter - 2;
        }
        else {
          input_fd = STDIN_FILENO;
        }

        // Checking if the command is NULL
        if (dupArray[0] == NULL){
          goto beginFor;
        }

        // Forking to execute the command
        pid_t pid;
        pid = fork();

        if (pid == -1){
          fprintf(stderr, "smallsh: Fork failed\n");
          goto free;
        }
        if (pid == 0){
          // Child
          // Resetting signals
          reset_signals();

          // Checking if redirect is needed
          if (output_fd != STDOUT_FILENO){
            if (dup2(output_fd, STDOUT_FILENO) == -1){
              fprintf(stderr, "Error: dup2 failed\n");
              goto free;
            }
            close(output_fd);
          }
         
          if (input_fd != STDIN_FILENO){
            if (dup2(input_fd, STDIN_FILENO) == -1){
              fprintf(stderr, "Error: dup2 failed\n");
              goto free;
            }
            close(input_fd);
          }

          if (strstr(dupArray[0], "/") == NULL){
            if (execvp(dupArray[0], dupArray) == -1){
              fprintf(stderr, "smallsh: %s: command not found\n", dupArray[0]);
              exit(1);
              goto free;
            }
          }
          else {
            if (execv(dupArray[0], dupArray) == -1){
              fprintf(stderr, "smallsh: %s: No such file or directory\n", dupArray[0]);
              goto free;
            }
          }
        }
        else{
          // Default
          // Setting the $! variable to the background process
          if (background == 1){
            char pid_str[10];
            sprintf(pid_str, "%d", pid);
            strcpy(bg_pid, pid_str);
          }
          // If the command shouldn't be run in the background
          if (background == 0){
            int status;
            waitpid(pid, &status, 0);
            if (WIFEXITED(status)){
              int exit_status = WEXITSTATUS(status);
              char exit_status_str[10];
              sprintf(exit_status_str, "%d", exit_status);
              strcpy(fg_exit_status, exit_status_str);
              goto free;
            }
            else if (WIFSIGNALED(status)){
              // Child process terminated by signal
              int sigNum = WTERMSIG(status);
              int exit_status = 128 + sigNum;
              char exit_status_str[10];
              sprintf(exit_status_str, "%d", exit_status);
              strcpy(fg_exit_status, exit_status_str);
              goto free;
            }
            else if (WIFSTOPPED(status)){
              // Child process stopped by signal
              kill(pid, SIGCONT);
              fprintf(stderr, "Child process %d stopped. Continuing. \n", pid);
              char stopped_pid_str[10];
              sprintf(stopped_pid_str, "%d", pid);
              strcpy(bg_pid, stopped_pid_str);
              goto free;
            }
          }
            else{
              goto beginFor;
            }
        }
free:
        // Freeing allocated memory
        for (int j = 0; j < tokenCounter; j++) {
          free(dupArray[j]);
        }
        free(dupArray);
        free(line);
        line = NULL;

        goto beginFor;
      }
    }
  }
}
