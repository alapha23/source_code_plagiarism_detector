/*
 * 4190.203 System Programming
 * Shell Lab
 *
 * tsh - A tiny shell program with job control
 *
 * Name: <omitted>
 * Student id: <omitted>
 *
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

/*----------------------------------------------------------------------------
 * Functions that you will implement
 */

void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigint_handler(int sig);
void sigtstp_handler(int sig);

/*----------------------------------------------------------------------------*/

/* These functions are already implemented for your convenience */
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

  /* This one provides a clean way to kill the shell */
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
 * the foreground, wait for it to terminate and then return.  Note:
 * each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.
 */
void eval(char *cmdline)
{
	char *argv[MAXARGS];
	char buf[MAXLINE];
	int bg;
	pid_t pid;
	sigset_t sigs; /* Note. Since fork to make child process is the most important work, 
										any signals have to be blocked until fork ends. */
	
	strcpy(buf, cmdline);
	bg = parseline(buf, argv); /* parse. */
	if(argv[0] == NULL) return; /* It means just enter. Ignore. */
	if(sigemptyset(&sigs) < 0){ /* make empty signalset. */
		unix_error("Cannot make empty signalset");
	}
	if(sigaddset(&sigs, SIGCHLD) < 0){ /* add SIGCHLD on empty signalset. */
		unix_error("Cannot add SIGCHLD on signalset");
	}
	/* if(sigaddset(&sigs, SIGINT) < 0){ // Reference says SIGCHLD has to be blocked, but I also temporarily added SIGINT, SIGTSTP. 
		unix_error("Cannot add SIGINT on signalset");
	}
	if(sigaddset(&sigs, SIGTSTP) < 0){
		unix_error("Cannot add SIGTSTP on signalset");
	} */
	if(!builtin_cmd(argv)){ // Since builtin_cmd returns 1 iff it is a builtin command, ! is required.
		/* First, block SIGCHLD signals when execute builtin_cmd */
		if(sigprocmask(SIG_BLOCK, &sigs, NULL) < 0){ /* block signals in signalset, in this case, SIGCHLD */
			unix_error("Cannot block signals in signalset");
		}
		/* block SIGCHLD success */

		if((pid = fork()) < 0){
			unix_error("Error on fork when eval");
		}
		else if(pid == 0){
			if(sigprocmask(SIG_UNBLOCK, &sigs, NULL) < 0){ /* unblock sigs */
				unix_error("Cannot unblock signals in signalset");
			}
			if(setpgid(0, 0) < 0){
				unix_error("cannot split any other processes by setpgid");
			}; /* Fowllows reference. */
			if(execve(argv[0], argv, environ) < 0){
				printf("%s: Command not found\n", argv[0]);
				exit(0);
			}
		}
		else{
			if(!bg){ /* all processes have to be add on joblist with pid, 
									and by reference, cmdline also have to be added. */
				addjob(jobs, pid, FG, cmdline);
				if(sigprocmask(SIG_UNBLOCK, &sigs, NULL) < 0){
					/* Since SIGCHLD works when CPU time is given to parent process, 
					 * SIGCHLD have to be unblocked even here is parent side. */
					unix_error("Cannot unblock signals in signalset");
				}
				waitfg(pid);
			}
			else{ /* parent process doesn't wait any background process. 
							 Just print it's information. */
				addjob(jobs, pid, BG, cmdline);
				if(sigprocmask(SIG_UNBLOCK, &sigs, NULL) < 0){
					unix_error("Cannot unblock signals in signalset");
				}
				printf("[%d] (%d) %s", pid2jid(pid), pid, cmdline);
				/* Note that cmdline leads print \n on command line, so don't put \n. */
			}
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
int builtin_cmd(char **argv) /* return 1 when it is builtin_cmd */
{
	if(strcmp(argv[0], "quit") == 0){ /* do not just use "==". It causes bugs. */
		exit(0);
	}
	if(strcmp(argv[0], "jobs") == 0){ /* show jobs. */
		listjobs(jobs);
		return 1;
	}
	else if((strcmp(argv[0], "fg") == 0) || (strcmp(argv[0], "bg") == 0)){ /* It is do_bgfg's turn. */
		do_bgfg(argv);
		return 1;
	}
	else if(strcmp(argv[0], "&") == 0){ /* It is same as just enter. */
		return 1;
	}
  return 0;     /* not a builtin command */
}

/*
 * do_bgfg - Execute the builtin bg and fg commands
 */
void do_bgfg(char **argv)
{
	struct job_t *job; /* save such job in here */
	char *id = argv[1]; /* since real job number is in argv[1] */
	int jid;

	/* catch if id is not exists */
	if(id == NULL){
		printf("%s command requires PID or %%jobid argument\n", argv[0]);
		return;
	}
	/* If it is a jid (%number), apply atoi on &id[1], and getjobjid */
	else if(id[0] == '%'){
		jid = atoi(&id[1]);
		if((job = getjobjid(jobs, jid)) == 0){
			printf("%s: No such job\n", id); // follow reference error msg
			return;
		}
	}
	/* else check if it is just id (number) by isdigit(id[0]), then apply atoi on id, and getjobpid */
	else if(isdigit(id[0])){
		pid_t pid = atoi(id);
		if((job = getjobpid(jobs, pid)) == 0){
			printf("(%d): No such process\n", pid); // follow reference error msg
			return;
		}
	}
	/* else tell user that it must be jid or pid, and return. */
	else{
		printf("%s: argument must be a PID or %%jobid\n", argv[0]); // follow reference error msg
		return;
	}

	/* Send SIGCONT to such process group with error checking by kill(...) < 0.
	 * When error occured, be careful that errno == ESRCH means 
	 * there doesn't exist progess group number, 
	 * not means kill failed by other reason. */

	/* if argv[0] == fg (strcmp), change job's state as FG 
	 * and wait until foreground job ends */
	if(strcmp(argv[0], "fg") == 0){
		if(job->state == ST){
			if(kill(-(job->pid), SIGCONT) < 0){
				if(errno != ESRCH)
					unix_error("kill error");
			}
		}
		job->state = FG; // If job->state is not ST, it is not need to be continued by signal.
		waitfg(job->pid);
	}
	/* else if argv[0] == bg, print background job's information,
	 * and change job's state as BG */
	else if(strcmp(argv[0], "bg") == 0){
		if(job->state == ST){
			if(kill(-(job->pid), SIGCONT) < 0){
				if(errno != ESRCH)
					unix_error("kill error");
			}
			job->state = BG; // Similar. If job->state is not ST, it is not need to be continued by signal.
			printf("[%d] (%d) %s", job->jid, job->pid, job->cmdline); /* same reason which used above */
		}
	}
	/* else, it is not valid, print bg/fg error: %s. But it won't occur in these trace files. */
	else{
		printf("bg/fg error: %s\n", argv[0]);
	}

}

/*
 * waitfg - Block until process pid is no longer the foreground process
 */
void waitfg(pid_t pid)
{
	struct job_t *job = getjobpid(jobs, pid); /* Actually, waitfg is called only if pid is for foreground job. 
																							 However, this function doesn't know whether or not it is foreground job.
																							 Instead, filltering is in below( ... == FG). */
	if(job == NULL) return;

  while((job->state == FG) && (pid == job->pid)){ /* wait that forground jobs. 
																										 sleep(1) gives sufficient term to make these method stable. */
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
 *     available zombie children, but doesn't wait for any other
 *     currently running children to terminate.
 */
void sigchld_handler(int sig)
{
	int status;
	pid_t pid;
	/* Check with while loop with finding pid, and check signals(STOPPED, INTERRUPTED, EXITED) */
	while((pid = waitpid(-1, &status, WUNTRACED|WNOHANG)) > 0){
		int jid = pid2jid(pid);
		if(WIFSTOPPED(status)){
			printf("Job [%d] (%d) stopped by signal %d\n", jid, pid, 20);
			struct job_t *job = getjobpid(jobs, pid);
			job->state = ST;
		}
		else if(deletejob(jobs, pid)){ /* else, that job must be deleted in this shell. 
																			It means we don't need to check WIFEXITED(status). */
			if(WIFSIGNALED(status)){
				printf("Job [%d] (%d) terminated by signal %d\n", jid, pid, 2);
			}
		}
	}
  return;
}

/*
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.
 */
void sigint_handler(int sig)
{
	pid_t pid = fgpid(jobs); /* send SIGINT to that foreground job */

	if(pid != 0){ /* fgpid(jobs) return positive value only 
									 when foreground job exists. else it returns 0. */	
		if(kill(-pid, SIGINT) < 0){ /* reference says -pid required to send SIGINT 
													 to all process in foreground process group. */
			if(errno != ESRCH) /* Here, ESRCH just means that pid or process group doesn't exist.
														Not an error of kill itself. */
				unix_error("kill error");
		}
	}
  return;
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.
 */
void sigtstp_handler(int sig)
{
	pid_t pid = fgpid(jobs);

	if(pid > 0){ /* similar reason like above handler */
		if(kill(-pid, SIGTSTP) < 0){
			if(errno != ESRCH) /* similar reason like above error handler */
				unix_error("kill error");
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
int maxjid(struct job_t *jobs)
{
  int i, max=0;

  for (i = 0; i < MAXJOBS; i++)
    if (jobs[i].jid > max)
      max = jobs[i].jid;
  return max;
}

/* addjob - Add a job to the job list */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline)
{
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
 * app_error - application-style error routine
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
  action.sa_flags = SA_RESTART; /* restart syscalls if possible */

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



