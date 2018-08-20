/*
 * M1522.000800 System Programming
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

/*----------------------------------------------------------------------------
 * error strings - predefined error string format
 */
#define SIGEMPTYSET 0
#define SIGADDSET 1
#define SIGPROCMASK 2
#define FORK 3
#define SETPGID 4
#define KILL 5
#define WAITPID 6
#define EXECVE 7
#define NULLARG 8
#define INVALIDARG 9
#define NOJOB 10
#define NOPROC 11
#define BGFG 12

static char *errstr[] = {
	"sigemptyset error",
	"sigaddset error",
	"sigprocmask error",
	"forking error",
	"setpgid error",
	"kill error",
	"waitpid error",
	"%s: Command not found\n",
	"%s command requires PID or %%jobid argument\n",
	"%s: argument must be a PID or %%jobid\n",
	"%s: No such job\n",
	"(%d): No such process\n",
	"bg/fg error: %s\n",
};
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------
 * print format strings - predefined output string format
 */

#define BACKGROUND 0
#define TERMINATED 1
#define STOPPED 2
static char *formatstr[] = {
	"[%d] (%d) %s",
	"Job [%d] (%d) terminated by signal %d\n",
	"Job [%d] (%d) stopped by signal %d\n",
};
/*----------------------------------------------------------------------------*/

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

/*----------------------------------------------------------------------------
 * System call wrapper functions prototype
 */

void Sigemptyset(sigset_t *set);
void Sigaddset(sigset_t *set, int signum);
void Sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
void Setpgid(pid_t pid, pid_t pgid);
void Execve(const char *filename, char *const argv[], char *const envp[]);
void Kill(pid_t pid, int sig);
pid_t Fork(void);
pid_t Waitpid(pid_t pid, int *status, int options);
/*----------------------------------------------------------------------------*/

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

	//printf("cmdline:%s\n",cmdline);
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
	// exception check
	if(cmdline == NULL)
		return;

	char *argv[MAXARGS] = {NULL,};
	char buf[MAXLINE];
	int is_bg;
	pid_t pid;
	sigset_t set;

	// copy command line string to buf. 
	strncpy(buf, cmdline, MAXLINE);

	// parse command line string. split it with white space and store it to argv.
	is_bg = parseline(buf, argv);
	
	// if empty command line string. ignore it.
	if(argv[0] == NULL)
		return;

	// if it is built-in command, command will be executed in builtin_cmd function immediately.
	// O.W. it must be a executable file, so execute it in below.
	if(builtin_cmd(argv) == 0)
	{
		// initialize the given signal set to empty.
		Sigemptyset(&set);

		// add signal SIGCHLD, which is generated when the child process is stopped or terminated
		// and sent to the parent process,to the given signal set.
		Sigaddset(&set, SIGCHLD);

		// block signal SIGCHLD in order to avoid race condition in parent process
		// if not, when parent calls addjob after fork. it might be affected by sigchld_handler.
		Sigprocmask(SIG_BLOCK, &set, NULL);
		
		// fork the process.
		pid = Fork();

		// child
		if(pid == 0)
		{
			// unblock signal SIGCHLD before executing new program
			Sigprocmask(SIG_UNBLOCK, &set, NULL);
			
			// to ensure that only one foreground job in the shell.
			// put the child process in the new process group with its group id equals child's pid
			Setpgid(0,0);

			// execute the program with envrionment
			Execve(argv[0], argv, environ);
		}

		//parent
		else
		{
			// add the child process to jobs
			addjob(jobs, pid, (is_bg ? BG:FG), cmdline);
			
			// unblock the signal SIGCHLD
			Sigprocmask(SIG_UNBLOCK, &set, NULL);

			// if background job then print predefined output and return to shell prompt
			if(is_bg)
				printf(formatstr[BACKGROUND], pid2jid(pid), pid, cmdline);
			// if foreground job then wait it and after child terminated return to shell prompt
			else		
				waitfg(pid);
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
	// exception check.
	if(argv == NULL || argv[0] == NULL)
		return 0;

	// jobs command
	if(strcmp(argv[0], "jobs") == 0)
	{
		listjobs(jobs);
		return 1;
	}
	// bg or fg command
	else if(strcmp(argv[0], "bg") == 0
		|| strcmp(argv[0], "fg") == 0)
	{
		do_bgfg(argv);
		return 1;
	}
	// quit command
	else if(strcmp(argv[0], "quit") == 0)
	{
		exit(0);
	}
	//else if(strcmp(argv[0], "&") == 0)
	//	return 1;
	
	return 0;     /*not a builtin command */
}

/*
 * check the argument id is valid.
 * if id is process id returns 0, else if job id returns 1, O.W. , not valid, returns -1.
 * */
int check_id(char *id)
{
	int is_jid = (id[0] == '%');
	int i;

	for(i = is_jid; i < strlen(id); i++)
		if(!isdigit(id[i]))
			return -1;

	return is_jid;
}

/*
 * do_bgfg - Execute the builtin bg and fg commands
 */
void do_bgfg(char **argv)
{
	// check if the argument is exists.
	if(argv == NULL || argv[1] == NULL)
	{
		printf(errstr[NULLARG], argv[0]);
		return;
	}
	struct job_t *job;
	int id;

	// check if the argument is valid.
	int is_jid = check_id(argv[1]);
	
	// if argument is job id
	if(is_jid == 1)
	{
		id = atoi(&(argv[1][1]));
		job = getjobjid(jobs, id);
	}
	// if argument is process id.
	else if(is_jid == 0)
	{
		id = atoi(&(argv[1][0]));
		job = getjobpid(jobs, id);
	}
	// if argument is not valid
	else
	{
		printf(errstr[INVALIDARG], argv[0]);
		return;
	}
	
	// fail to find the job with pid/jid
	if(job == NULL)
	{
		if(is_jid)
			printf(errstr[NOJOB], argv[1]);
		else
			printf(errstr[NOPROC], id);
		return;
	}

	// send SIGCONT signal, to restart process.
	Kill(-(job->pid), SIGCONT);

	// if command is bg. set state to background.
	if(strcmp(argv[0], "bg") == 0)
	{
		printf(formatstr[BACKGROUND], job->jid, job->pid, job->cmdline);
		job->state = BG;
	}
	// if command is fg. set state to foreground
	else if(strcmp(argv[0], "fg") == 0)
	{
		job->state = FG;
		waitfg(job->pid);// wait it terminated or stopped or goto background.
	}
	// invalid command
	else
	{
		printf(errstr[BGFG], argv[0]);
	}
  	
	return;
}

/*
 * waitfg - Block until process pid is no longer the foreground process
 */
void waitfg(pid_t pid)
{
	// get job by its process id
	struct job_t *job;
	if((job = getjobpid(jobs, pid)) == NULL)
		return;
	
	// busy waiting. constantly check if it's state is FG
	while(job->state == FG)
		sleep(0);
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
	int all_child = -1;
	pid_t pid;

	// check all terminated child and do proper processing.
	while((pid = Waitpid(all_child, &status, WNOHANG | WUNTRACED)) > 0)
	{
		// if exited normally, reap the child
		if(WIFEXITED(status))
			deletejob(jobs, pid);

		// if stopped handle it by calling sigstp_handler
		if(WIFSTOPPED(status))
			sigtstp_handler(-SIGTSTP);

		// if signaled, handle it by calling sigint_handler
		if(WIFSIGNALED(status))
			sigint_handler(-SIGINT);
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
	// get foreground process id
	pid_t pid = fgpid(jobs);
	if(pid != 0)
	{
		// if some process get the signal send signal to all process in the same process group
		if(sig > 0)
			Kill(-pid, sig);
		// if sig == 0 it is used to check valiadation of pid
		else if(sig == 0)
			Kill(pid, sig);
		// delete job from the list
		if(sig < 0)
		{	
			printf(formatstr[TERMINATED], pid2jid(pid), pid, (-sig));
			deletejob(jobs, pid);
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
	// get foreground process id
	pid_t pid = fgpid(jobs);
	if(pid != 0)
	{
		struct job_t *job;
		if(sig > 0)
		{
			Kill(-pid, sig);
			// set state of the foreground job to stopped
			if((job = getjobpid(jobs, pid)) != NULL)
				job->state = ST;
			printf(formatstr[STOPPED], pid2jid(pid), pid, sig);
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


/***********************************
 * System call wrapper functions
 ***********************************/

/*
 * Sigemptyset - wrapper for sigemptyset function
 * if fails, i.e. return value is -1, call unix_error to exit program.
 * */
void Sigemptyset(sigset_t *set)
{
	if(sigemptyset(set) != 0)
		unix_error(errstr[SIGEMPTYSET]);
}

/*
 * Sigaddset - wrapper for sigaddset function
 * if fails, i.e. return value is -1, call unix_error to exit program.
 * */
void Sigaddset(sigset_t *set, int signum)
{
	if(sigaddset(set, signum) != 0)
		unix_error(errstr[SIGADDSET]);
}

/*
 * Sigprocmask - wrapper for sigprocmask function
 * if fails, i.e. return value is -1, call unix_error to exit program.
 * */
void Sigprocmask(int how, const sigset_t *set, sigset_t *oldset)
{
	if(sigprocmask(how, set, oldset) != 0)
		unix_error(errstr[SIGPROCMASK]);
}

/*
 * Setpgid - wrapper for setpgid function
 * if fails, i.e. return value is -1, call unix_error to exit program.
 * */
void Setpgid(pid_t pid, pid_t pgid)
{
	if(setpgid(pid, pgid) < 0)
		unix_error(errstr[SETPGID]);
		
}

/*
 * Execve - wrapper for execve function
 * if fails, i.e. return value is -1, print output in predefined format and exit.
 * */
void Execve(const char *filename, char *const argv[], char *const envp[])
{
	if(execve(filename, argv, envp) < 0)
	{
		printf(errstr[EXECVE], argv[0]);
		exit(1);
	}
}

/*
 * Kill - wrapper for kill function
 * if fails, i.e. return value is -1, call unix_error to exit program.
 * */
void Kill(pid_t pid, int sig)
{
	if(kill(pid, sig) != 0)
		unix_error(errstr[KILL]);
}

/*
 * Fork - wrapper for fork function. return pid.
 * if fails, i.e. return value is -1, call unix_error to exit program.
 * */
pid_t Fork()
{
	pid_t pid;
	
	if((pid = fork()) < 0)
		unix_error(errstr[FORK]);

	return pid;
}
/*
 * Waitpid - wrapper for waitpid function. return pid
 * if fails, i.e. return value is -1 and errno != ECHILD, call unix_error to exit program.
 * */
pid_t Waitpid(pid_t pid, int *status, int options)
{
	pid_t reaped_pid;

	if((reaped_pid = waitpid(pid, status, options))< 0 && errno != ECHILD)
		unix_error(errstr[WAITPID]);

	return reaped_pid;
}

/***********************************
 * End System call wrapper functions
 ***********************************/
