#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include<signal.h>

# define N_CHAR 100

// Initialize with null character
char prev_command[N_CHAR] = {'\0'};  

// Initialize all required words buffer
char echo_trigger[] = "echo";
char echo_ec[] = "echo $?";
char exit_trigger[] = "exit";
char prev_trigger[] = "!!";
char shell_comment[] = "##";
const char delimiter[] = " ";

// Not sure if this is optimum inits
int fg_pid = 0; 
int prev_exit_status = 0;

int process_command(char command[], int script_mode) {

    int run = 1;
    char *token;
    char *tmp = (char *) malloc(strlen(command) + 1);

    strcpy(tmp, command);
    token = strtok(tmp, delimiter);

    if(token != NULL) {
        if(!strcmp(token, echo_trigger)){
            if(!strcmp(command, echo_ec)) {
                printf("%d", prev_exit_status);
            }
            else {
                token = strtok(NULL, delimiter);
                while(token != NULL) {
                    printf("%s ", token);
                    token = strtok(NULL, delimiter);
                }
            }
            printf("\n");
            prev_exit_status = 0;
        } 
        else if (!strcmp(token, exit_trigger)) {
            token = strtok(NULL, delimiter);
            long code = (int) strtol(token, NULL, 10);
            if(!script_mode) printf("Exiting program with code %ld\n", code);
            exit(code);
        } 
        else if (!strcmp(token, prev_trigger)) { 
            // Guard if no prev command exist yet
            if(prev_command[0] != '\0'){
                // Print the prev_command if not in script mode.
                if(!script_mode) printf("%s\n", prev_command);
                return process_command(prev_command, script_mode);
            }  
            prev_exit_status = 0;
        } else {
            int status;
            int pid;
            int i = 0;
            // Init tokens
            char * prog_arv[N_CHAR];
            prog_arv[i] = token;
            while(token != NULL) {
                token = strtok(NULL, delimiter);
                prog_arv[++i] = token;
            }
            prog_arv[i+1] = NULL;

            if((pid=fork()) < 0) {
                perror("Fork failled");
            } 
            if(!pid) {
                execvp(prog_arv[0], prog_arv);
            }
            if (pid) {
                fg_pid = pid;
                waitpid(pid, &status, 0);
                prev_exit_status = WEXITSTATUS(status);
                fg_pid = 0;
            }
        }
        // If condition is here as strcpy same string content leads to early abortion.
        if(strcmp(prev_command, command)) strcpy(prev_command, command);
    }

    return run;
}

int run_command(char command[], int script_mode) {
    // Parse new line from fgets. 
    // src: https://stackoverflow.com/questions/2693776/removing-trailing-newline-character-from-fgets-input
    command[strcspn(command, "\n")] = 0;
    // If command is not empty and not shell comment
    if(command[0] != 0 && strncmp(command, shell_comment, strlen(shell_comment))) {
        return process_command(command, script_mode);
    }
    return 1;
}

void read_file(char fileName[]) {
    FILE* file = fopen(fileName, "r");
    char line[256];
    while(fgets(line, sizeof(line), file)) {
        run_command(line, 1);
    }
}

void fg_handler() {
    if(fg_pid != 0) {
        kill(fg_pid, SIGINT);
    }
}

void stop_handler() {
    if(fg_pid != 0) {
        kill(fg_pid, SIGTSTP);
    }
}

void init_sas() {
    struct sigaction sa, oldsa;
    struct sigaction sh, oldsh;

    // ctrl+c = SIGINT
    sa.sa_handler = fg_handler;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, &oldsa);

    // ctrl+z = SIGTSTP
    sh.sa_handler = stop_handler;
    sh.sa_flags = 0;
    sigemptyset(&sh.sa_mask);
    sigaction(SIGTSTP, &sh, &oldsh);
}

int main(int argc, char *argv[]) {
    printf("Starting IC shell\n");
    init_sas();

    char command[N_CHAR];
    char args[N_CHAR];
    
    if(argc == 2) {
        read_file(argv[1]);
        return 0;
    }

    printf("icsh $ <waiting for command>\n");
    while (1) {
        printf("icsh $ ");
        if(fgets(command, N_CHAR, stdin) != NULL){
            run_command(command, 0);
        }
    }
    
    return 0;
}
