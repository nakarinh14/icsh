#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

# define N_CHAR 100

// Initialize with null character
char prev_command[N_CHAR] = {'\0'};  

// Initialize all buffer for command words
char echo_trigger[] = "echo";
char exit_trigger[] = "exit";
char prev_trigger[] = "!!";
const char delimiter[] = " ";

int process_command(char command[]) {
    int run = 1;
    char *token;
    char *tmp = (char *) malloc(strlen(command) + 1);

    strcpy(tmp, command);
    token = strtok(tmp, delimiter);

    if(token != NULL) {
        if(!strcmp(token, echo_trigger)){
            token = strtok(NULL, delimiter);
            while(token != NULL) {
                printf("%s ", token);
                token = strtok(NULL, delimiter);
            }
            printf("\n");
        } 
        else if (!strcmp(token, exit_trigger)) {
            token = strtok(NULL, delimiter);
            long code = strtol(token, NULL, 10);
            printf("Exiting program with code %ld\n", code);
            exit(code);
        } 
        else if (!strcmp(token, prev_trigger)) { 
            if(prev_command[0] != '\0'){
                printf("%s", prev_command);
                return process_command(prev_command);
            }  
        } else {
            printf("bad command\n");
        }
        
        strcpy(prev_command, command);
    }

    return run;
}

int main() {
    printf("Starting IC shell\n");
    printf("icsh $ <waiting for command>\n");

    int run = 1;
  
    char command[N_CHAR];
    char args[N_CHAR];
    
    while (run) {
        printf("icsh $ ");
        if(fgets(command, N_CHAR, stdin) != NULL){
            // Parse new line from fgets. src: https://stackoverflow.com/questions/2693776/removing-trailing-newline-character-from-fgets-input
            command[strcspn(command, "\n")] = 0;
            if(command[0] != 0) {
                run = process_command(command);
            }
        }
    }
    
    return 0;
}
