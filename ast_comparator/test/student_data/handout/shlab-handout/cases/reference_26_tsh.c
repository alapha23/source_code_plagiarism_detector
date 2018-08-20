/*
 * tsh - A tiny shell program with job control
 *
 * Sukrit Chhabra - sc3349
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <assert.h>
#include <fcntl.h>

/* Misc manifest constants */
#define MAXLINE    1024   /* max line size */
#define MAXARGS     128   /* max args on a command line */
#define MAXJOBS      16   /* max jobs at any point in time */
#define MAXJID    1<<16   /* max job ID */

/* Job states */
#define UNDEF 0 /* undefined */
#define FG 1    /* running in foreground */
#define BG 2    /* running in background */
#define ST 3    /* stopped */

/*
 * Jobs states: FG (foreground), BG (background), ST (stopped)
 * Job state transitions and enabling actions:
 *     FG -> ST  : ctrl-z
 *     ST -> FG  : fg command
 *     ST -> BG  : bg command
 *     BG -> FG  : fg command
 * At most 1 job can be in the FG state.
 */

/* Defining Parsing states for tokens */
#define ST_NORMAL   0x0   /* next token is an argument */
#define ST_INFILE   0x1   /* next token is the input file */
#define ST_OUTFILE  0x2   /* next token is the output file */


/* Global variables */
extern char **environ;      /* defined in libc */
char prompt[] = "tsh> ";    /* command line prompt (DO NOT CHANGE) */
int verbose = 0;            /* if true, print additional output */
int nextjid = 1;            /* next job ID to allocate */
char sbuf[MAXLINE];         /* for composing sprintf messages */

struct job_t {              /* The job struct */
    pid_t pid;              /* job PID */
    int jid;                /* job ID [1, 2, ...] */
    int state;              /* UNDEF, BG, FG, or ST */
    char cmdline[MAXLINE];  /* command line */
};
struct job_t jobs[MAXJOBS]; /* The job list */


/* Creating a struct for command line tokens */
struct cmdline_tokens {
    int argc;               /* Number of arguments */
    char *argv[MAXARGS];    /* The arguments list */
    char *infile;           /* The input file */
    char *outfile;          /* The output file */
    enum builtins_t {       /* Indicates if argv[0] is a builtin command */
        BUILTIN_NONE,
        BUILTIN_QUIT,
        BUILTIN_JOBS,
        BUILTIN_BG,
        BUILTIN_FG} builtins;
};

/* End global variables */


/* Function prototypes */
void eval(char *cmdline);
int builtin_cmd(struct cmdline_tokens cmd_tokens); // Passing cmd_tokens instead of char** argv
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

/* Here are helper routines that we've provided for you */
int parseline(const char *cmdline, struct cmdline_tokens *cmd_tokens); // Passing pointer to cmd_tokens instead of char** argv
void sigquit_handler(int sig);

void clearjob(struct job_t *job);
void initjobs(struct job_t *jobs);
int maxjid(struct job_t *jobs);
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline);
int deletejob(struct job_t *jobs, pid_t pid);
pid_t fgpid(struct job_t *jobs);
struct job_t *getjobpid(struct job_t *jobs, pid_t pid);
struct job_t *getjobjid(struct job_t *jobs, int jid);
int pid2jid(pid_t pid);
void listjobs(struct job_t *jobs, int output_fd);

void usage(void);
void unix_error(char *msg);
void app_error(char *msg);
typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);



/*
 * main - The shell's main routine
 */
int main(int argc, char **argv) {
    char c;
    char cmdline[MAXLINE];
    int emit_prompt = 1; /* emit prompt (default) */

    /* Redirect stderr to stdout (so that driver will get all output
     * on the pipe connected to stdout) */
    dup2(1, 2);

    /* Parse the command line */
    while ((c = getopt(argc, argv, "hvp")) != EOF) {
        switch (c) {
        case 'h':             /* print help message */
            usage();
        break;
        case 'v':             /* emit additional diagnostic info */
            verbose = 1;
        break;
        case 'p':             /* don't print a prompt */
            emit_prompt = 0;  /* handy for automatic testing */
        break;
    default:
            usage();
    }
    }

    /* Install the signal handlers */

    /* These are the ones you will need to implement */
    Signal(SIGINT,  sigint_handler);   /* ctrl-c */
    Signal(SIGTSTP, sigtstp_handler);  /* ctrl-z */
    Signal(SIGCHLD, sigchld_handler);  /* Terminated or stopped child */
    Signal(SIGTTIN, SIG_IGN); // Declaring for signal handlers
    Signal(SIGTTOU, SIG_IGN); // Declaring for signal handlers

    /* This one provides a clean way to kill the shell */
    Signal(SIGQUIT, sigquit_handler);

    /* Initialize the job list */
    initjobs(jobs);


    /* Execute the shell's read/eval loop */
    while (1) {

        if (emit_prompt) {
            printf("%s", prompt);
            fflush(stdout);
        }
        if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
            app_error("fgets error");
        if (feof(stdin)) {
            /* End of file (ctrl-d) */
            fflush(stdout);
            exit(0);
        }

        /* Remove the trailing newline */
        cmdline[strlen(cmdline)-1] = '\0';

        /* Evaluate the command line */
        eval(cmdline);

        fflush(stdout);
        fflush(stdout);
    }

    exit(0); /* control never reaches here */
}

/*
 * eval - Evaluate the command line that the user has just typed in
 *
 * If the user has requested a built-in command (quit, jobs, bg or fg)
 * then execute it immediately. Otherwise, fork a child process and
 * run the job in the context of the child. If the job is running in
 * the foreground, wait for it to terminate and then return.  Note:
 * each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.
 */

void eval(char *cmdline) {
    int bg;                              // Background or foreground
    struct cmdline_tokens cmd_tokens;    // Command line tokens
    pid_t pid;                           // Processing ID
    int fd;                              // File discriptor
    int oldfd;                           // Temp file discriptor
    struct job_t *pjob;                  // Job pointer
    sigset_t sigchld_mask;               // SIGCHLD mask
    sigset_t sigint_mask;                // SIGINT mask
    sigset_t sigtstp_mask;               // SIGTSTP mask

    /* Initializing Masks */
    if (sigemptyset(&sigchld_mask)) {
        unix_error("main: sigemptyset error");
        exit(1);
    }
    if (sigaddset(&sigchld_mask, SIGCHLD)) {
        unix_error("main: sigaddset error");
        exit(1);
    }
    if (sigemptyset(&sigint_mask)) {
        unix_error("main: sigemptyset error");
        exit(1);
    }
    if (sigaddset(&sigint_mask, SIGINT)) {
        unix_error("main: sigaddset error");
        exit(1);
    }
    if (sigemptyset(&sigtstp_mask)) {
        unix_error("main: sigemptyset error");
        exit(1);
    }
    if (sigaddset(&sigtstp_mask, SIGTSTP)) {
        unix_error("main: sigaddset error");
        exit(1);
    }

    bg = parseline(cmdline, &cmd_tokens) + 1;
    if (bg == 0 || cmd_tokens.argv[0] == NULL) {
    // If line is empty, i.e., there are no arguments
        return;
    }
    if (!builtin_cmd(cmd_tokens)) {
        if (addjob(jobs, 1, bg, cmdline)) {
        // If job list is not full
        // TODO: find tutorial link and add here
            if (sigprocmask(SIG_BLOCK, &sigchld_mask, NULL)) {
                unix_error("eval: sigprocmask error");
                exit(1);
            }
            if (sigprocmask(SIG_BLOCK, &sigint_mask, NULL)) {
                unix_error("eval: sigprocmask error");
                exit(1);
            }
            if (sigprocmask(SIG_BLOCK, &sigtstp_mask, NULL)) {
                unix_error("eval: sigprocmask error");
                exit(1);
            }

            if ((pid = fork()) == 0) {
                if (sigprocmask(SIG_UNBLOCK, &sigchld_mask, NULL)) {
                    unix_error("eval: sigprocmask error");
                    exit(1);
                }

                if (sigprocmask(SIG_UNBLOCK, &sigint_mask, NULL)) {
                    unix_error("eval: sigprocmask error");
                    exit(1);
                }

                if (sigprocmask(SIG_UNBLOCK, &sigtstp_mask, NULL)) {
                    unix_error("eval: sigprocmask error");
                    exit(1);
                }

                if (setpgid(0, 0)) { // Set group ID
                    unix_error("eval: setpgid error");
                    exit(1);
                }
                oldfd = 0;
                if (cmd_tokens.infile) {
                    fd = open(cmd_tokens.infile, O_RDONLY, 0);
                    dup2(fd, STDIN_FILENO);
                }

                if (cmd_tokens.outfile) {
                    oldfd = open(cmd_tokens.outfile, O_RDONLY, 0);
                    dup2(STDOUT_FILENO, oldfd);
                    fd = open(cmd_tokens.outfile, O_WRONLY, 0);
                    dup2(fd, STDOUT_FILENO);
                }

                if (execve(cmd_tokens.argv[0], cmd_tokens.argv, environ) < 0) {
                    if (oldfd) {
                        dup2(oldfd, STDOUT_FILENO);
                    }
                    printf("%s: Command not found\n", cmd_tokens.argv[0]);
                    exit(0);
                }
            }

            if (nextjid == 1) {
                pjob = getjobjid(jobs, MAXJOBS);
            } else {
                pjob = getjobjid(jobs, nextjid - 1);
            }

            pjob->pid = pid;
            if (sigprocmask(SIG_UNBLOCK, &sigint_mask, NULL)) {
                unix_error("eval: sigprocmask error");
                exit(1);
            }
            if (sigprocmask(SIG_UNBLOCK, &sigtstp_mask, NULL)) {
                unix_error("eval: sigprocmask error");
                exit(1);
            }
            if (bg == FG) {     // Foreground job
                waitfg(pid);
            } else {            // Background job
                printf("[%d] (%d) %s\n", pjob->jid, pjob->pid, pjob->cmdline);
            }
            if (sigprocmask(SIG_UNBLOCK, &sigchld_mask, NULL)) {
                unix_error("eval: sigprocmask error");
                exit(1);
            }
        } else {
            printf("eval: addjob error\n");
        }
    }
    return;
}

/*
 * parseline - Parse the command line and build the argv array.
 *
 * Parameters:
 *   cmdline:  The command line, in the form:
 *
 *                command [arguments...] [< infile] [> oufile] [&]
 *
 *   cmd_tokens:      Pointer to a cmdline_tokens structure. The elements of this
 *             structure will be populated with the parsed tokens. Characters
 *             enclosed in single or double quotes are treated as a single
 *             argument.
 * Returns:
 *   1:        if the user has requested a BG job
 *   0:        if the user has requested a FG job
 *  -1:        if cmdline is incorrectly formatted
 *
 * Note:       The string elements of cmd_tokens (e.g., argv[], infile, outfile)
 *             are statically allocated inside parseline() and will be
 *             overwritten the next time this function is invoked.
 */
int parseline(const char *cmdline, struct cmdline_tokens *cmd_tokens) {
    // TODO: find tutorial link and add here
    /* Changing parseline to parse struct cmd_tokens */
    static char array[MAXLINE];          /* holds local copy of command line */
    const char delims[10] = " \t\r\n";   /* argument delimiters (white-space) */
    char *buf = array;                   /* ptr that traverses command line */
    char *next;                          /* ptr to the end of the current arg */
    char *endbuf;                        /* ptr to end of cmdline string */
    int is_bg;                           /* background job? */

    int parsing_state;                   /* indicates if the next token is the
                                            input or output file */

    if (cmdline == NULL) {
        (void) fprintf(stderr, "Error: command line is NULL\n");
        return -1;
    }

    (void) strncpy(buf, cmdline, MAXLINE);
    endbuf = buf + strlen(buf);

    cmd_tokens->infile = NULL;
    cmd_tokens->outfile = NULL;

    /* Build the argv list */
    parsing_state = ST_NORMAL;
    cmd_tokens->argc = 0;

    while (buf < endbuf) {
        /* Skip the white-spaces */
        buf += strspn (buf, delims);
        if (buf >= endbuf) break;

        /* Check for I/O redirection specifiers */
        if (*buf == '<') {
            if (cmd_tokens->infile) {
                (void) fprintf(stderr, "Error: Ambiguous I/O redirection\n");
                return -1;
            }
            parsing_state |= ST_INFILE;
            buf++;
            continue;
        }
        if (*buf == '>') {
            if (cmd_tokens->outfile) {
                (void) fprintf(stderr, "Error: Ambiguous I/O redirection\n");
                return -1;
            }
            parsing_state |= ST_OUTFILE;
            buf ++;
            continue;
        }

        if (*buf == '\'' || *buf == '\"') {
            /* Detect quoted tokens */
            buf++;
            next = strchr (buf, *(buf-1));
        } else {
            /* Find next delimiter */
            next = buf + strcspn (buf, delims);
        }

        if (next == NULL) {
            /* Returned by strchr(); this means that the closing
               quote was not found. */
            (void) fprintf (stderr, "Error: unmatched %c.\n", *(buf-1));
            return -1;
        }

        /* Terminate the token */
        *next = '\0';

        /* Record the token as either the next argument or the i/o file */
        switch (parsing_state) {
            case ST_NORMAL:
                cmd_tokens->argv[cmd_tokens->argc++] = buf;
                break;
            case ST_INFILE:
                cmd_tokens->infile = buf;
                break;
            case ST_OUTFILE:
                cmd_tokens->outfile = buf;
                break;
            default:
                (void) fprintf(stderr, "Error: Ambiguous I/O redirection\n");
                return -1;
        }
        parsing_state = ST_NORMAL;

        /* Check if argv is full */
        if (cmd_tokens->argc >= MAXARGS-1) break;

        buf = next + 1;
    }

    if (parsing_state != ST_NORMAL) {
        (void) fprintf(stderr,
                "Error: must provide file name for redirection\n");
        return -1;
    }

    /* The argument list must end with a NULL pointer */
    cmd_tokens->argv[cmd_tokens->argc] = NULL;

    if (cmd_tokens->argc == 0)  /* ignore blank line */
        return 1;

    if (!strcmp(cmd_tokens->argv[0], "quit")) {                 /* quit command */
        cmd_tokens->builtins = BUILTIN_QUIT;
    } else if (!strcmp(cmd_tokens->argv[0], "jobs")) {          /* jobs command */
        cmd_tokens->builtins = BUILTIN_JOBS;
    } else if (!strcmp(cmd_tokens->argv[0], "bg")) {            /* bg command */
        cmd_tokens->builtins = BUILTIN_BG;
    } else if (!strcmp(cmd_tokens->argv[0], "fg")) {            /* fg command */
        cmd_tokens->builtins = BUILTIN_FG;
    } else {
        cmd_tokens->builtins = BUILTIN_NONE;
    }

    /* Should the job run in the background? */
    if ((is_bg = (*cmd_tokens->argv[cmd_tokens->argc-1] == '&')) != 0)
        cmd_tokens->argv[--cmd_tokens->argc] = NULL;

    return is_bg;
}

/*
 * builtin_cmd - If the user has typed a built-in command then execute
 *    it immediately.
 */
int builtin_cmd(struct cmdline_tokens cmd_tokens) {
    int i;
    char **argv;
    /**
     * NOTE:
     * Based on tutorial the cmd_tokens were to be passed to builtin_cmd
     * But earlier argv was being passed.
     * Still using argv but getting that from cmd_tokens instead of passing to the function directly
     */
    int fd; // File discriptor


    switch (cmd_tokens.builtins) {
        case BUILTIN_QUIT:
        // Quit Command
            exit(0);
            return 1;
            break;
        case BUILTIN_BG: // Command background
        case BUILTIN_FG: // Command foreground
            argv = cmd_tokens.argv;     // Getting argv here
            if (argv[1] == NULL) {
                printf("There are no arguments!");
            } else {
                i = 0;
                if (argv[1][0] == '%') {
                    i = 1;
                }
                while (argv[1][i] != '\0') {
                    if (!isdigit(argv[1][i])) {
                        break;
                    }
                    ++i;
                }
                if (argv[1][i] == '\0') {
                    do_bgfg(argv);
                } else {
                    printf("%s: argument must be a PID or %%jobid\n", argv[0]);
                }
            }
            return 1;
            break;
        case BUILTIN_JOBS:
            if (cmd_tokens.outfile) {
                fd = open(cmd_tokens.outfile, O_WRONLY, 0);
                listjobs(jobs, fd);
                close(fd);
            } else {
                listjobs(jobs, STDOUT_FILENO);
            }
            return 1; /* Builtin command */
            break;
        default: return 0; /* Not a builtin command */
    }
}

/*
 * do_bgfg - Execute the builtin bg and fg commands
 */
void do_bgfg(char **argv) {
    int id;
    int job_or_process;
    struct job_t *pjob; // Pointer to job
    int state;
    sigset_t sigchld_mask; // SIGCHLD mask

    // Mask initialization
    if (sigemptyset(&sigchld_mask)) {
        unix_error("main: sigemptyset error");
        exit(1);
    }
    if (sigaddset(&sigchld_mask, SIGCHLD)) {
        unix_error("main: sigaddset error");
        exit(1);
    }

    if (sigprocmask(SIG_BLOCK, &sigchld_mask, NULL)) {
        unix_error("do_bgfg: sigprocmask error");
        exit(1);
    }
    if (argv[1][0] == '%') {
    // If arg is Job ID
        job_or_process = 1;
        id = atoi((argv[1]) + 1);
    } else {
    // If arg is Process ID
        job_or_process = 0;
        id = atoi(argv[1]);
    }
    if (job_or_process) {
        pjob = getjobjid(jobs, id);
    } else {
        pjob = getjobpid(jobs, id);
    }
    if (pjob) {
        state = pjob->state;
        if (argv[0][0] == 'b' && state == ST) {
            pjob->state = BG; // Change state to background
            printf("[%d] (%d) %s", pjob->jid, pjob->pid, pjob->cmdline);
            if (kill(-(pjob->pid), SIGCONT)) { // If state is ST Send SIGCONT signal
                unix_error("do_bgfg: kill error");
                exit(1);
            }
        } else if (argv[0][0] == 'f' && (state == ST || state == BG)) {
            pjob->state = FG; // Change state to foreground
            if (state == ST) {
                if (kill(-(pjob->pid), SIGCONT)) {
                    unix_error("do_bgfg: kill error");
                    exit(1);
                }
            }
            waitfg(pjob->pid);
        }
    } else {
        if (job_or_process) {
            printf("%%%d: No such job\n", id);
        } else {
            printf("(%d): No such process\n", id);
        }
    }
    if (sigprocmask(SIG_UNBLOCK, &sigchld_mask, NULL)) {
        unix_error("do_bgfg: sigprocmask error");
        exit(1);
    }
    return;
}

/*
 * waitfg - Block until process pid is no longer the foreground process
 */
void waitfg(pid_t pid) {
    int status; // Wait status
    struct job_t *pjob; // Job pointer
    int ret; // Wait return value


    do {
        ret = waitpid(pid, &status, WUNTRACED); // Wait for terminated or stopped
    } while (ret == -1 && errno == EINTR); // In case being interrupted
    if (ret == -1 && errno == ECHILD) { // No such child process
        unix_error("waitfg: waitpid error");
        exit(1);
    } else if (ret > 0) { // Terminated or stopped
        if (WIFSTOPPED(status)) { // Stopped
            if ((pjob = getjobpid(jobs, pid)) != NULL) { // Get job
                pjob->state = ST; // Change state
                printf("Job [%d] (%d) stopped by signal 20\n", pjob->jid, pjob->pid);
            } else { // Invalid job
                printf("waitfg: getjobpid error\n");
                exit(1);
            }
        } else if (WIFSIGNALED(status)) { // Terminated by other process signal
            if ((pjob = getjobpid(jobs, pid)) != NULL) { // Get job
                printf("Job [%d] (%d) terminated by signal 2\n", pjob->jid, pjob->pid);
                if (!deletejob(jobs, pid)) { // Delete job
                    printf("waitfg: deletejob error\n");
                    exit(1);
                }
            } else { // Invalid job
                printf("waitfg: getjobpid error\n");
                exit(1);
            }
        } else if (WIFEXITED(status)) {
            if (!deletejob(jobs, pid)) {
                printf("waitfg: deletejob error\n");
                exit(1);
            }
        }
    }
    return;
}

/*****************
 * Signal handlers
 *****************/

/*
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *     a child job terminates (becomes a zombie), or stops because it
 *     received a SIGSTOP or SIGTSTP signal. The handler reaps all
 *     available zombie children, but doesn't wait for any other
 *     currently running children to terminate.
 */
void sigchld_handler(int sig) {
    int status;         // Status
    pid_t pid;          // pid
    struct job_t *pjob; // Pointer to job

    do {
    // Following directions from the link in the README
        pid = waitpid(-1, &status, WNOHANG|WUNTRACED); // Using WNOHANG as spoken about in class
        if (pid > 0) {
            if (WIFSTOPPED(status)) {
            // If stopped
                if ((pjob = getjobpid(jobs, pid)) != NULL) {
                // get job and change state
                    pjob->state = ST; // changing state
                    printf("Job [%d] (%d) stopped by signal 20\n", pjob->jid, pjob->pid);
                } else {
                // Job was invalid
                    printf("sigchld_handler: getjobpid error\n");
                    exit(1);
                }
            } else if (WIFSIGNALED(status)) {
                // TODO: Find the link for the second tutorial
                if ((pjob = getjobpid(jobs, pid)) != NULL) {
                    printf("Job [%d] (%d) terminated by signal 2\n", pjob->jid, pjob->pid);
                    if (!deletejob(jobs, pid)) { // Delete job
                        printf("sigchld_handler: deletejob error\n");
                        exit(1);
                    }
                } else {
                    printf("sigchld_handler: getjobpid error\n");
                    exit(1);
                }
            } else if (WIFEXITED(status)) {
                if (!deletejob(jobs, pid)) {
                    // DELETE JOB
                    printf("sigchld_handler: deletejob error\n");
                    exit(1);
                }
            } else {
                printf("Unknown waitpid\n");
                exit(1);
            }
        } else if (pid == -1 && errno == EINTR) {
            continue;
        } else {
            break;
        }
    } while (1);
    return;
}

/*
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.
 */
void sigint_handler(int sig) {
    pid_t pid;


    if ((pid = fgpid(jobs)) != 0) { // Get foreground job
        if (kill(-pid, SIGKILL)) { // Send SIGKILL signal
            unix_error("sigint_handler: kill error");
            exit(1);
        }
    }
    return;
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.
 */
void sigtstp_handler(int sig) {
    pid_t pid;


    if ((pid = fgpid(jobs)) != 0) { // Get foreground job
        if (kill(-pid, SIGTSTP)) { // Send SIGTSTP signal
            unix_error("sigtstp_handler: kill error");
            exit(1);
        }
    }
    return;
}

/*********************
 * End signal handlers
 *********************/

/***********************************************
 * Helper routines that manipulate the job list
 **********************************************/

/* clearjob - Clear the entries in a job struct */
void clearjob(struct job_t *job) {
    job->pid = 0;
    job->jid = 0;
    job->state = UNDEF;
    job->cmdline[0] = '\0';
}

/* initjobs - Initialize the job list */
void initjobs(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
        clearjob(&jobs[i]);
}

/* maxjid - Returns largest allocated job ID */
int maxjid(struct job_t *jobs) {
    int i, max=0;

    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].jid > max)
            max = jobs[i].jid;
    return max;
}

/* addjob - Add a job to the job list */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline) {
    int i;

    if (pid < 1)
        return 0;

    for (i = 0; i < MAXJOBS; i++) {
    if (jobs[i].pid == 0) {
        jobs[i].pid = pid;
        jobs[i].state = state;
        jobs[i].jid = nextjid++;
        if (nextjid > MAXJOBS)
            nextjid = 1;
        strcpy(jobs[i].cmdline, cmdline);
        if(verbose){
            printf("Added job [%d] %d %s\n", jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
            }
            return 1;
    }
    }
    printf("Tried to create too many jobs\n");
    return 0;
}

/* deletejob - Delete a job whose PID=pid from the job list */
int deletejob(struct job_t *jobs, pid_t pid) {
    int i;

    if (pid < 1)
        return 0;

    for (i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid == pid) {
            clearjob(&jobs[i]);
            nextjid = maxjid(jobs)+1;
            return 1;
        }
    }
    return 0;
}

/* fgpid - Return PID of current foreground job, 0 if no such job */
pid_t fgpid(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].state == FG)
            return jobs[i].pid;
    return 0;
}

/* getjobpid  - Find a job (by PID) on the job list */
struct job_t *getjobpid(struct job_t *jobs, pid_t pid) {
    int i;

    if (pid < 1)
        return NULL;
    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].pid == pid)
            return &jobs[i];
    return NULL;
}

/* getjobjid  - Find a job (by JID) on the job list */
struct job_t *getjobjid(struct job_t *jobs, int jid) {
    int i;

    if (jid < 1)
        return NULL;
    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].jid == jid)
            return &jobs[i];
    return NULL;
}

/* pid2jid - Map process ID to job ID */
int pid2jid(pid_t pid) {
    int i;

    if (pid < 1)
        return 0;
    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].pid == pid) {
            return jobs[i].jid;
        }
    return 0;
}

/* listjobs - Print the job list */
void listjobs(struct job_t *jobs, int output_fd) {
    int i;
    char buf[MAXLINE];

    for (i = 0; i < MAXJOBS; i++) {
        memset(buf, '\0', MAXLINE);
    if (jobs[i].pid != 0) {
        sprintf(buf, "[%d] (%d) ", jobs[i].jid, jobs[i].pid);
        if(write(output_fd, buf, strlen(buf)) < 0) {
            fprintf(stderr, "Error writing to output file\n");
            exit(1);
        }
        memset(buf, '\0', MAXLINE);
        switch (jobs[i].state) {
        case BG:
            printf("Running ");
            break;
        case FG:
            printf("Foreground ");
            break;
        case ST:
            printf("Stopped ");
            break;
        default:
            printf("listjobs: Internal error: job[%d].state=%d ", i, jobs[i].state);
        }
        if(write(output_fd, buf, strlen(buf)) < 0) {
            fprintf(stderr, "Error writing to output file\n");
            exit(1);
        }
        memset(buf, '\0', MAXLINE);
        sprintf(buf, "%s\n", jobs[i].cmdline);
        if(write(output_fd, buf, strlen(buf)) < 0) {
            fprintf(stderr, "Error writing to output file\n");
            exit(1);
        }
    }
    }
}
/******************************
 * end job list helper routines
 ******************************/


/***********************
 * Other helper routines
 ***********************/

/*
 * usage - print a help message
 */
void usage(void) {
    printf("Usage: shell [-hvp]\n");
    printf("   -h   print this message\n");
    printf("   -v   print additional diagnostic information\n");
    printf("   -p   do not emit a command prompt\n");
    exit(1);
}

/*
 * unix_error - unix-style error routine
 */
void unix_error(char *msg) {
    fprintf(stdout, "%s: %s\n", msg, strerror(errno));
    exit(1);
}

/*
 * app_error - application-style error routine
 */
void app_error(char *msg) {
    fprintf(stdout, "%s\n", msg);
    exit(1);
}

/*
 * Signal - wrapper for the sigaction function
 */
handler_t *Signal(int signum, handler_t *handler) {
    struct sigaction action, old_action;

    action.sa_handler = handler;
    sigemptyset(&action.sa_mask); /* block sigs of type being handled */
    action.sa_flags = SA_RESTART; /* restart syscalls if possible */

    if (sigaction(signum, &action, &old_action) < 0)
        unix_error("Signal error");
    return (old_action.sa_handler);
}

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 */
void sigquit_handler(int sig) {
    printf("Terminating after receipt of SIGQUIT signal\n");
    exit(1);
}