#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <ctype.h>

# define N_CHAR 256
# define N_PID 4194304
# define N_JOBS 1000

struct job {
    char command[N_CHAR];
    int pid;
    int job_id;
    int status; // 0: Foreground process, 1: Background running process, 2: Stopped process
};

// Initialize with null character
char prev_command[N_CHAR] = {'\0'};  

// Initialize all required words buffer
char echo_trigger[] = "echo";
char echo_ec[] = "echo $?";
char exit_trigger[] = "exit";
char prev_trigger[] = "!!";
char jobs_trigger[] = "jobs";
char fg_trigger[] = "fg";
char bg_trigger[] = "bg";
char shell_comment[] = "##";
const char ws_delim[] = " ";

// Not sure if this is optimum inits
int prev_exit_status = 0;
pid_t ppid;
pid_t jobs_to_pid[N_JOBS];
pid_t jobs_order[2];
struct job pids_command[N_PID];

int current_nb = 0;

// Saved current terminal STDOUT in saved_stdout
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

/* Dealing with I/O redirection functions */

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

/* Dealing with foregorund and background process */

void swap_jobs_order(int nb) {
    jobs_order[1] = jobs_order[0];
    jobs_order[0] = nb;
}

char *get_job_status(int status) {
    switch (status){
        case 1:
            return "Running";
        case 2:
            return "Stopped";
        default:
            return "";
    }
}

int parse_amp_job(char* token) {
    if(token == NULL || token[0] != '%') {
        printf("Invalid arguments: %s\n", token);
        return 0;
    }
    const char t[2] = "%";
    char* tmp;
    tmp = strtok(token, t);
    return atoi(tmp);
}

char get_jobs_sign(pid_t job_id) {
    if(jobs_order[0] == job_id) 
        return '+';
    else if(jobs_order[1] == job_id) 
        return '-';
    else
        return ' ';
}

void process_fg(char* token) {
    int job_id = parse_amp_job(token);
    pid_t pid = jobs_to_pid[job_id];
    if(pid) {
        printf("%s\n", pids_command[pid].command);
        swap_jobs_order(job_id);
        int status;
        setpgid(pid, pid);
        tcsetpgrp (0, pid);
        kill(pid, SIGCONT);
        waitpid(pid, &status, 0);
        tcsetpgrp (0, ppid);
        prev_exit_status = WEXITSTATUS(status);
    } else if (job_id){
        printf("fg: %%%d: no such job\n", job_id);
    }
}

void process_bg(char *token) {
    int job_id = parse_amp_job(token);
    pid_t pid = jobs_to_pid[job_id];
    if(pid) {
        struct job* job_ = &pids_command[pid];
        char sign = get_jobs_sign(job_->job_id);
        printf("[%d]%c %s &\n",  job_->job_id, sign, job_->command);
        job_->status = 1;
        kill(pid, SIGCONT);
    }
    else if (job_id > 0){
        printf("bg: %%%d: no such job\n", job_id);
    }
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

void get_jobs() {
    for(int i = 1; i <= current_nb; i++) {
        pid_t pid = jobs_to_pid[i];
        if(pid != 0) {
            struct job* job_ = &pids_command[pid];
            if(job_ != NULL && job_->status > 0) {
                char sign = get_jobs_sign(i);
                printf("[%d]%c  %s                    %s &\n", 
                i, sign, get_job_status(job_->status), job_->command);
            }
        }
    }
}

/* Main functions to process commands */
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
        else if(!strcmp(command, jobs_trigger)) {
            get_jobs();
        }
        else if(!strcmp(token, fg_trigger)) {
            token = strtok(NULL, ws_delim);
            process_fg(token);
        } 
        else if(!strcmp(token, bg_trigger)) {
            token = strtok(NULL, ws_delim);
            process_bg(token);
        }
        else {
            int status;
            int pid;
            if((pid=fork()) < 0) {
                perror("Fork failled");
            } 
            else if(!pid) {
                setpgid(0, 0);
                
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
                // Record pids
                struct job curr_job;
                strcpy(curr_job.command , command);
                curr_job.pid = pid;
                curr_job.status = bgp;
                curr_job.job_id = bgp ? ++current_nb : 0;
                pids_command[pid] = curr_job;
                setpgid(pid, pid);

                if(!bgp) {
                    tcsetpgrp (0, pid);
                    waitpid(pid, &status, 0);
                    tcsetpgrp (0, ppid);
                    prev_exit_status = WEXITSTATUS(status);
                } else {
                    jobs_to_pid[current_nb] = pid;
                    // Switch order of 2nd latest background run
                    swap_jobs_order(current_nb);
                    printf("[%d] %d\n", current_nb, pid);
                }
            }
        }
        // If condition is here as strcpy same string content leads to early abortion.
        if(strcmp(prev_command, command)) 
            strcpy(prev_command, command);
    }
    free(tmp);
}

int run_command(char command[], int script_mode) {
    // Parse new line from fgets. 
    // src: https://stackoverflow.com/questions/2693776/removing-trailing-newline-character-from-fgets-input
    command[strcspn(command, "\n")] = 0;
    // If command is not empty and not shell comment
    if(command[0] != 0 && strncmp(command, shell_comment, strlen(shell_comment)))
        process_command(command, script_mode);
    return 1;
}

void read_file(char fileName[]) {
    FILE* file = fopen(fileName, "r");
    char line[256];
    while(fgets(line, sizeof(line), file)) 
        run_command(line, 1);
}

void fg_handler() {}

void stop_handler() {}

void ChildHandler (int sig, siginfo_t *sip, void *notused){
    int status = 0;
   /* The WNOHANG flag means that if there's no news, we don't wait*/
    if (sip->si_pid == waitpid (sip->si_pid, &status, WNOHANG)){
        /* A SIGCHLD doesn't necessarily mean death - a quick check */
        if (WIFEXITED(status)|| WTERMSIG(status)) {
            struct job* job_ = &pids_command[sip->si_pid];
            if(job_ != NULL && job_->status && job_->job_id) {
                char sign = get_jobs_sign(job_->job_id);
                printf("\n[%d]%c  Done                    %s\n",  job_->job_id, sign, job_->command);
                fflush (stdout); 
                // Nullified the job in buffer.
                jobs_to_pid[job_->job_id] = 0;
                struct job null_job;
                null_job.job_id = 0;
                null_job.pid = 0;
                pids_command[sip->si_pid] = null_job;
            }
        } 
    } 
    else {
        // Just lazily assume the child is stopped in the else clause lol.
        struct job* job_ = &pids_command[sip->si_pid];
        if(job_ != NULL) {
            // If not in any job queue (fresh foreground job), add it.
            if(!(job_->job_id)) {
                job_->job_id = ++current_nb;
                jobs_to_pid[job_->job_id] = sip->si_pid;
                swap_jobs_order(job_->job_id);
            } 
            // Else, just update job status as Stopped (2)
            job_->status = 2;
            char sign = get_jobs_sign(job_->job_id);
            printf("\n[%d]%c  Stopped                    %s\n",  job_->job_id, sign, job_->command);
            fflush (stdout); 
        }
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
    
    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);

    struct sigaction action;
    action.sa_sigaction = ChildHandler; /* Note use of sigaction, not    
                                        handler */
    sigfillset (&action.sa_mask);
    action.sa_flags = SA_SIGINFO; /* Note flag,otherwise NULL in function*/
    sigaction (SIGCHLD, &action, NULL);
}

int main(int argc, char *argv[]) {
    memset (pids_command, 0, N_PID);
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
        memset (command, 0, N_CHAR);
        printf("icsh $ ");
        if(fgets(command, N_CHAR, stdin) != NULL)
            run_command(command, 0);
    }
    
    return 0;
}
