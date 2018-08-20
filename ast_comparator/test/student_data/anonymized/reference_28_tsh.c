/* 
 * tsh - A tiny shell program with job control
 */
#include 
#include 
#include 
#include 
#include 
#include 
#include <sys/types.h>
#include <sys/wait.h>
#include 
The various header files are used in this programming homework help file for different tasks and functionalities. 
All the input and output functions are accessed from the stdio.h. the memory allocation functionalities, process control, 
conversions and other similar tasks are performed by the stdlib.h file. 
The miscellaneous symbolic programming assignments help constants and types like fpathconf() , pathconf(), sysconf() 
functionalities are accessed from the unistd.h file. 
All the string relted functions are accessed from the string.h file. The numerous standard library functions are accessed from the ctype.h file. 
The signal.h file is used to access the symbolic constants. To access the system clock time sys/types.h file is used. Similarly the sys/wait.h 
file is used for the symbolic constants for use with waitpid() which does not hang out the system if no do my programming homework status found to be
 available and returns immediately.The errno.h header file has been used for handling the different types of error values.

/* Misc manifest constants */
#define MAXLINE    1024   /* max line size */
#define MAXARGS     128   /* max args on a command line */
#define MAXJOBS      16   /* max jobs at any point in time */
#define MAXJID    1< ST  : ctrl-z
 *     ST -> FG  : fg command
 *     ST -> BG  : bg command
 *     BG -> FG  : fg command
 * At most 1 job can be in the FG state.
 */

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
/* End global variables */

/* Function prototypes */

/* Here are the functions that you will implement */
void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

/* Here are helper routines that we've provided for you */
int parseline(const char *cmdline, char **argv); 
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
void listjobs(struct job_t *jobs);

void usage(void);
void unix_error(char *msg);
void app_error(char *msg);
typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);

/*
 * main - The shell's main routine 
 */
int main(int argc, char **argv) 
{
  char c;
  char cmdline[MAXLINE];
  int emit_prompt = 1; /* emit prompt (default) */

  /* Redirect stderr to stdout pay for programming assignment  (so that driver will get all output
   * on the pipe connected to stdout) */
  dup2(1, 2);

  /* Parse the command line */
  while ((c = getopt(argc, argv, "hvp")) != EOF) {
    switch (c) {
    case 'h':             /* print help message */
      usage();
      break;
    case 'v':             /* emit additional programming homework help diagnostic info */
      verbose = 1;
      break;
    case 'p':             /* don't print a prompt */
      emit_prompt = 0;  /* handy pay for programming assignment for automatic testing */
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

  /* This one provides a programming homework for money clean way to kill the shell */
  Signal(SIGQUIT, sigquit_handler); 

  /* Initialize the job list */
  initjobs(jobs);

  /* Execute the shell's read/eval loop */
  while (1) {

    /* Read command line */
    if (emit_prompt) {
      printf("%s", prompt);
      fflush(stdout);
    }

    if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
      app_error("fgets error");
    if (feof(stdin)) { /* End of file (ctrl-d) */
      fflush(stdout);
      exit(0);
    }

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
 * the foreground, wait for help with programming assignment  it to terminate and then return.  Note:
 * each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.  
 */
void eval(char *cmdline) 
{
  /* the following code demonstrates how to use parseline --- you'll 
   * want to replace most of help with programming homework  it (at least the print statements). */
  int bg;
  char *argv[MAXARGS];
  sigset_t set;

  bg = parseline(cmdline, argv);

  if (argv[0] == NULL) return; /* ignore empty lines */

  /* try to execute as a builtin-cmd */
  if (builtin_cmd(argv)) {
     return;
  }

  /* mask SIGCHLD before create the process */
  sigemptyset(&set);
  sigaddset(&set, SIGCHLD);
  sigprocmask(SIG_BLOCK, &set, NULL);

  /* disable SIGTTOUT and make my programming assignment  SIGTTIN for job controls*/
  Signal(SIGTTOU, SIG_IGN);
  Signal(SIGTTIN, SIG_IGN);

  /* fork a child process to execute it */
  pid_t child = fork();
  if (child == -1) {
    printf("Error: could not fork child process.\n");
    return;
  }

  if (child > 0) {
    /* parent process. add to job list and unblock SIGCHLD */
    addjob(jobs, child, bg ? BG : FG, cmdline);
    if (bg) {
      /* display information about the job */
      struct job_t *job = getjobpid(jobs, child);
      printf("[%d] (%d) %s", job->jid, job->pid, cmdline);
    }
    sigemptyset(&set);
    sigaddset(&set, SIGCHLD);
    sigprocmask(SIG_UNBLOCK, &set, NULL);

    /* if not bg, wait until it's not the foreground
     * process */
    if (!bg) {
      waitfg(child);
    }
  } else {
    /* child process. create a new group for this process and 
     * unmark the SIGCHLD */
    setpgid(0, 0);
    sigemptyset(&set);
    sigaddset(&set, SIGCHLD);
    sigprocmask(SIG_UNBLOCK, &set, NULL);

    /* execute the program in the child process*/
    if (execvp(argv[0], argv) < 0) {
      printf("%s: Command not found.\n", argv[0]);
      exit(-1);
    }
  }
}

/* 
 * parseline - Parse the command line and build the argv array.
 * 
 * Characters enclosed in single quotes are treated as a single
 * argument.  Return true if the user has requested a BG job, false if
 * the user has requested a FG job.  
 */

int parseline(const char *cmdline, char **argv) 
{
  static char array[MAXLINE]; /* holds local copy of command line */
  char *buf = array;          /* ptr that traverses command line */
  char *delim;                /* points to first space delimiter */
  int argc;                   /* number of args */
  int bg;                     /* background job? */

  strcpy(buf, cmdline);
  buf[strlen(buf)-1] = ' ';  /* replace trailing '\n' with space */
  while (*buf && (*buf == ' ')) /* ignore leading spaces */
    buf++;

  /* Build the argv list */
  argc = 0;
  if (*buf == '\'') {
    buf++;
    delim = strchr(buf, '\'');
  }
  else {
    delim = strchr(buf, ' ');
  }

  while (delim) {
    argv[argc++] = buf;
  *delim = '\0';
    buf = delim + 1;
    while (*buf && (*buf == ' ')) /* ignore spaces */
      buf++;

    if (*buf == '\'') {
      buf++;
      delim = strchr(buf, '\'');
    }
    else {
      delim = strchr(buf, ' ');
    }
  }
  argv[argc] = NULL;

  if (argc == 0)  /* ignore blank line */
    return 1;

  /* should the job run in the background? */
  if ((bg = (*argv[argc-1] == '&')) != 0) {
    argv[--argc] = NULL;
  }
  return bg;
}

/* 
 * builtin_cmd - If the user has typed a built-in command then execute
 *    it immediately.  
 */
int builtin_cmd(char **argv) 
{
   if (strcmp(argv[0], "quit") == 0) {
     kill(getpid(), SIGKILL);
     return 1;
   }
   if (strcmp(argv[0], "jobs") == 0) {
     listjobs(jobs);
     return 1;
   }
   if (strcmp(argv[0], "bg") == 0 || strcmp(argv[0], "fg") == 0) {
     do_bgfg(argv);
     return 1;
   }
   return 0;     /* not a builtin command */
}

/* 
 * do_bgfg - Execute the builtin bg and fg commands
 */
void do_bgfg(char **argv) 
{
  struct job_t *job;
int i;

  /* parameter checking */
  if (argv[1] == NULL) {
     printf("%s command requires PID or %%jobid argument\n", argv[0]);
     return;
  }
  if (argv[1][0] == '%') { /* parse the job id */
     int jid = 0;
     for (i = 1; argv[1][i] != 0; i++) {
        if (argv[1][i] < '0' || argv[1][i] > '9') {
           printf("%s: argument must be a PID or %%jobid\n", argv[0]);
           return;
        }
        jid = jid * 10 + argv[1][i] - '0';
     }
     job = getjobjid(jobs, jid);
     if (job == NULL) {
       printf("%s: No such job\n", argv[1]);
       return;
     }
  } else { /* parse the process id */
     int pid = 0; 
     for (i = 0; argv[1][i] != 0; i++) {
        if (argv[1][i] < '0' || argv[1][i] > '9') {
           printf("%s: argument must be a PID or %%jobid\n", argv[0]);
           return;
        }
        pid = pid * 10 + argv[1][i] - '0';
     }
     job = getjobpid(jobs, pid);
     if (job == NULL) {
       printf("(%s): No such process\n", argv[1]);
       return;
     }
  }
    if (strcmp(argv[0], "fg") == 0) {
      /* turn the job to foreground */
      tcsetpgrp(STDIN_FILENO, job->pid);
      tcsetpgrp(STDOUT_FILENO, job->pid);

      if (job->state == ST) {
		 /* send a SIGCONT to continue the stopped job */
         kill(-job->pid, SIGCONT);
	  }
      job->state = FG;

      /* wait until the job is not fore ground */
      waitfg(job->pid);

      /* return the foreground to the shell */
      tcsetpgrp(STDIN_FILENO, getpid());
      tcsetpgrp(STDOUT_FILENO, getpid());
kill(-getpid(), SIGCONT);
      return;
    }

    if (strcmp(argv[0], "bg") == 0) {
      /* turn the job to background */
      printf("[%d] (%d) %s", job->jid, job->pid, job->cmdline);
      job->state = BG;
      kill(-job->pid, SIGCONT);
      return;
    }

  return;
}

/* 
 * waitfg - Block until process pid is no longer the foreground process
 */
void waitfg(pid_t pid)
{
  while (fgpid(jobs) == pid) {
    sleep(1);
  }
}

/*****************
 * Signal handlers
 *****************/

/* 
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *     a child job terminates (becomes a zombie), or stops because it
 *     received a SIGSTOP or SIGTSTP signal. The handler reaps all
 *     available zombie children, but doesn't programming assignment help wait for any other
 *     currently running children to terminate.  
 */
void sigchld_handler(int sig) 
{
  int stat;
  pid_t child;
  while ((child = waitpid(-1, &stat, WNOHANG | WUNTRACED)) > 0) {
    struct job_t *job = getjobpid(jobs, child);
    /* check if the child is stopped or done */
    if (WIFSTOPPED(stat)) {
      printf("Job [%d] (%d) stopped by signal %d\n", job->jid, job->pid, WSTOPSIG(stat));
      job->state = ST;
    } else {
      if (WIFSIGNALED(stat)) {
         printf("Job [%d] (%d) terminated by signal %d\n", job->jid, job->pid, WTERMSIG(stat));
      }
      /* remove the child from the job list */
      deletejob(jobs, child);
    }
  }
}

/* 
 * sigint_handler - The kernel sends a programming assignment help SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.  
 */
void sigint_handler(int sig) 
{
  /* get the foreground job pid and ssend the SIGINT to it. */
  pid_t fgid = fgpid(jobs);
  if (fgid > 0) {
    kill(-fgid, SIGINT);
  }
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.  
 */
void sigtstp_handler(int sig) 
{
  /* get the foreground job pid programming homework help and ssend the SIGTSTP to it. */
  pid_t fgid = fgpid(jobs);
  if (fgid > 0) {
    struct job_t *job = getjobpid(jobs, fgid);
    kill(-fgid, SIGTSTP);
    /* mark the job stopped */
    job->state = ST;
  }
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
int maxjid(struct job_t *jobs) 
{
  int i, max=0;

  for (i = 0; i < MAXJOBS; i++) if (jobs[i].jid > max)
      max = jobs[i].jid;
  return max;
}

/* addjob - Add a job to the job list */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline) 
{
  int i;

  if (pid < 1)
    return 0;

  for (i = 0; i < MAXJOBS; i++) { if (jobs[i].pid == 0) { jobs[i].pid = pid; jobs[i].state = state; jobs[i].jid = nextjid++; if (nextjid > MAXJOBS)
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
int deletejob(struct job_t *jobs, pid_t pid) 
{
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

/* getjobpid  - Find a job (by PID) on programming assignment help the job list */
struct job_t *getjobpid(struct job_t *jobs, pid_t pid) {
  int i;

  if (pid < 1)
    return NULL;
  for (i = 0; i < MAXJOBS; i++)
    if (jobs[i].pid == pid)
      return &jobs[i];
  return NULL;
}

/* getjobjid  - Find a job (by JID) programming homework help on the job list */
struct job_t *getjobjid(struct job_t *jobs, int jid) 
{
  int i;

  if (jid < 1)
    return NULL;
  for (i = 0; i < MAXJOBS; i++)
    if (jobs[i].jid == jid)
      return &jobs[i];
  return NULL;
}

/* pid2jid - Map process ID to job ID */
int pid2jid(pid_t pid) 
{
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
void listjobs(struct job_t *jobs) 
{
  int i;

  for (i = 0; i < MAXJOBS; i++) {
    if (jobs[i].pid != 0) {
      printf("[%d] (%d) ", jobs[i].jid, jobs[i].pid);
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
        printf("listjobs: Internal error: job[%d].state=%d ", 
               i, jobs[i].state);
      }
      printf("%s", jobs[i].cmdline);
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
void usage(void) 
{
  printf("Usage: shell [-hvp]\n");
  printf("   -h   print this message\n");
  printf("   -v   print additional diagnostic information\n");
  printf("   -p   do not emit a command prompt\n");
  exit(1);
}

/*
 * unix_error - unix-style error routine
 */
void unix_error(char *msg)
{
  fprintf(stdout, "%s: %s\n", msg, strerror(errno));
  exit(1);
}

/*
 * app_error - application-style programming homework help error routine
 */
void app_error(char *msg)
{
  fprintf(stdout, "%s\n", msg);
  exit(1);
}

/*
 * Signal - wrapper for the sigaction function
 */
handler_t *Signal(int signum, handler_t *handler) 
{
  struct sigaction action, old_action;

  action.sa_handler = handler;  
  sigemptyset(&action.sa_mask); /* block sigs of type being handled */
  action.sa_flags = SA_RESTART; /* restart programming assignment help syscalls if possible */

  if (sigaction(signum, &action, &old_action) < 0)
    unix_error("Signal error");
  return (old_action.sa_handler);
}

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 */
void sigquit_handler(int sig) 
{
  printf("Terminating after receipt of SIGQUIT signal\n");
  exit(1);
}