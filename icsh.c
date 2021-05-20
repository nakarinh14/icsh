#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include<signal.h>
#include <errno.h>
#include <fcntl.h>
#include <ctype.h>

# define N_CHAR 100

// Initialize with null character
char prev_command[N_CHAR] = {'\0'};  

// Initialize all required words buffer
char echo_trigger[] = "echo";
char echo_ec[] = "echo $?";
char exit_trigger[] = "exit";
char prev_trigger[] = "!!";
char jobs_trigger[] = "jobs";
char shell_comment[] = "##";
const char ws_delim[] = " ";

// Not sure if this is optimum inits
int fg_pid = 0; 
int cjob_id = 0;
int prev_exit_status = 0;
int ppid;

// Saved current terminal STDOUT
int saved_stdout; 

char *trim_ws(char *command){
    char *tmp = (char *) malloc(strlen(command) + 1);
    strcpy(tmp, command);
    char *end;
    // https://stackoverflow.com/questions/122616/how-do-i-trim-leading-trailing-whitespace-in-a-standard-way

    // Trim leading space
    while(isspace((unsigned char)*tmp)) tmp++;
    if(*tmp == 0)  // All spaces?
    return tmp;

    // Trim trailing space
    end = tmp + strlen(tmp) - 1;
    while(end > tmp && isspace((unsigned char)*end)) end--;

    // Write new null terminator character
    end[1] = '\0';

    return tmp;
}

void redir_out(char *filename) {
    int fd = open(filename, O_TRUNC | O_CREAT | O_WRONLY, 0666);
    if ((fd <= 0)) {
        fprintf (stderr, "Couldn't open a file\n");
        exit (errno);
    }
    dup2(fd, STDOUT_FILENO); 
    dup2(fd, STDERR_FILENO);
    close(fd);
}

void redir_in(char *filename) {
    int in = open(filename, O_RDONLY);
    if ((in <= 0)) {
        fprintf (stderr, "Couldn't open a file\n");
        exit (errno);
    }
    dup2 (in, STDIN_FILENO);
    close(in);
}

char* check_io_redir(char command[]) {
    if(strchr(command, '>')  != NULL) 
        return ">";
    else if (strchr(command, '<') != NULL)
        return "<";
    return NULL;
}

void redir_int(char filename[]) { }

char* process_redir(char command[]) {
    char* redir_char = check_io_redir(command);

    if(redir_char != NULL) {

        char *token;
        char *tmp = (char *) malloc(strlen(command) + 1);
        strcpy(tmp, command);

        token = strtok(tmp, redir_char);
        char *parsed_command = trim_ws(token);
        token = strtok(NULL, redir_char);
        char *filename = trim_ws(token);

        if(strchr(redir_char, '>')) 
            redir_out(filename);
        else if (strchr(redir_char, '<')) 
            redir_in(filename);
        
        free(tmp);
        return parsed_command;
    }
    free(redir_char);
    return command;
}

int is_bgp(char command[]){
    if(command && *command && command[strlen(command) - 1] == '&') {
        command[strlen(command) - 1] = '\0';
        // Trim trailing space
        char *end;
        end = command + strlen(command) - 1;
        while(end > command && isspace((unsigned char)*end)) end--;
        // Write new null terminator character
        end[1] = '\0';
        return 1;
    }
    return 0;
}

void process_command(char command[], int script_mode) {
    char *token;
    char *tmp = (char *) malloc(strlen(command) + 1);
    
    int bgp = is_bgp(command);
    strcpy(tmp, command);
    
    token = strtok(tmp, ws_delim);

    if(token != NULL) {
        if(!strcmp(token, echo_trigger)){
            if(!strcmp(command, echo_ec)) {
                printf("%d", prev_exit_status);
            }
            else {
                token = strtok(NULL, ws_delim);
                while(token != NULL) {
                    printf("%s ", token);
                    token = strtok(NULL, ws_delim);
                }
            }
            printf("\n");
            prev_exit_status = 0;
        } 
        else if (!strcmp(token, exit_trigger)) {
            token = strtok(NULL, ws_delim);
            long code = (int) strtol(token, NULL, 10);
            if(!script_mode) 
                printf("Exiting program with code %ld\n", code % 256);
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
        }
        else if(!strcmp(token, jobs_trigger)) {
            printf("gettings jobs");
        }
        else {
            int status;
            int pid;
            if((pid=fork()) < 0) {
                perror("Fork failled");
            } 
            else if(!pid) {
                // setpgid(0,0);
                // tcsetpgrp (0, getpid());
                // All redirection initialized in child process
                command = process_redir(command);
                // Init tokens
                int i = 0;
                char * prog_arv[N_CHAR];
                token = strtok(command, ws_delim);
                prog_arv[i] = token;
                while(token != NULL) {
                    token = strtok(NULL, ws_delim);
                    prog_arv[++i] = token;
                }
                prog_arv[i+1] = NULL;
                execvp(prog_arv[0], prog_arv);
                exit(errno);
            }
            else if (pid) {
                fg_pid = pid;
                // if(!bgp) {
                //     setpgid(fg_pid, fg_pid);
                //     tcsetpgrp (0, fg_pid);
                //     waitpid(fg_pid, &status, 0);
                //     tcsetpgrp (0, ppid);
                //     prev_exit_status = WEXITSTATUS(status);
                // }
                waitpid(fg_pid, &status, 0);
                prev_exit_status = WEXITSTATUS(status);
                fg_pid = 0;
            }
        }
        // If condition is here as strcpy same string content leads to early abortion.
        if(strcmp(prev_command, command)) 
            strcpy(prev_command, command);
    }
    free(tmp);
    dup2(saved_stdout, 1); // Restore stdout if I/O redirection was enabled
}

int run_command(char command[], int script_mode) {
    // Parse new line from fgets. 
    // src: https://stackoverflow.com/questions/2693776/removing-trailing-newline-character-from-fgets-input
    command[strcspn(command, "\n")] = 0;
    // If command is not empty and not shell comment
    if(command[0] != 0 && strncmp(command, shell_comment, strlen(shell_comment))) {
        process_command(command, script_mode);
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

void sigttin_handler() {}
void sigttou_handle() {}

void init_sas() {
    struct sigaction sa, oldsa;
    struct sigaction sh, oldsh;
    struct sigaction stin, oldstin;
    struct sigaction stou, oldstou;

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

    stin.sa_handler = sigttin_handler;
    stin.sa_flags = 0;
    sigemptyset(&stin.sa_mask);
    sigaction(SIGTTIN, &stin, &oldstin);

    stou.sa_handler = sigttou_handle;
    stou.sa_flags = 0;
    sigemptyset(&stou.sa_mask);
    sigaction(SIGTTOU, &stou, &oldstou);
}

int main(int argc, char *argv[]) {
    ppid = getpid();
    init_sas();
    saved_stdout = dup(STDOUT_FILENO);

    char command[N_CHAR];
    char args[N_CHAR];
    
    if(argc == 2) {
        read_file(argv[1]);
        return 0;
    }
    printf("Starting IC shell\n");
    printf("icsh $ <waiting for command>\n");
    while (1) {
        printf("icsh $ ");
        if(fgets(command, N_CHAR, stdin) != NULL){
            run_command(command, 0);
        }
    }
    
    return 0;
}