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

/* ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■ *
 * Include and Declation
 * ■■■■                                                                 ■■■■■■ */
#include <string.h>

void checkQuit(char *cmd);
pid_t testWait();

static int getIntOrStp = 0;

// Switch of debug definition
// #define DEBUG

#ifdef DEBUG
	static int STEPINDEX = 0;
#endif
/* ■■■■                                                                 ■■■■■■ */
/* ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■ */


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

//   printf("parent id: %d \n", getpid());
  fflush(stdout);
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
/* ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■ *
 * Eval function
 * ■■■■                                                                 ■■■■■■ */

 	char *argv[MAXARGS];
	char buffer[MAXLINE];
	int fOrb;
	pid_t pid;
	int jid;
	
	sigset_t mask;
	strcpy(buffer, cmdline);
	fOrb = parseline(buffer, argv);

	// ignore empty commands
	if (NULL == argv[0]) {
		return;
	}

	if (!builtin_cmd(argv)) {

		sigemptyset(&mask);
		sigaddset(&mask, SIGCHLD);
		sigprocmask(SIG_BLOCK, &mask, NULL);

		#ifdef DEBUG
			printf("\033[1;30mNo.%d------------otherwise------------------------------\033[0m\n", ++STEPINDEX);
			printf("<<< \033[1;32m%s\033[0m\n", cmdline);
			fflush(stdout);
		#endif
		
		if ((pid = fork()) == 0) {
			setpgid(0, 0);
			sigprocmask(SIG_UNBLOCK, &mask, NULL);
			
			if (execve(argv[0], argv, environ) < 0) {
				printf("%s: Command not found\n", argv[0]);
				exit(0);
			}
		}

		if (fOrb) {
			// if it is a background job, parent add job 
			#ifdef DEBUG
				printf("\033[0;32;34mAddpid:\033[0m %d \n", pid);
			#endif

			addjob(jobs, pid, BG, cmdline);
			jid = pid2jid(pid);
			printf("[%d] (%d) %s", jid, pid, cmdline);
			sigprocmask(SIG_UNBLOCK, &mask, NULL);
		}else {
			// if it is a foreground job, parent wait
			#ifdef DEBUG
				printf("\033[0;35m(%d)job run in foreground! \033[0m\n", pid);
			#endif
			addjob(jobs, pid, FG, cmdline);
			sigprocmask(SIG_UNBLOCK, &mask, NULL);
			waitfg(pid);
		}
	}

/* ■■■■                                                                 ■■■■■■ */
/* ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■ */
  return;
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
/* ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■ *
 * built in command
 * ■■■■                                                                 ■■■■■■ */
	if (!strcmp(argv[0], "quit")) {
		// If quit
		//  kill(0, SIGQUIT);

		#ifdef DEBUG
			printf("\033[1;31mQuit!\033[0m \n");
		#endif
		exit(0);
	}else if (0 == strcmp(argv[0], "jobs")) {
		// If jobs
		#ifdef DEBUG
			printf("\033[1;30mNo.%d-----------------------jobs------------------------\033[0m\n", ++STEPINDEX);
		#endif
		
		listjobs(jobs);
		return 1;
	}else if (0 == strcmp(argv[0], "bg") || 0 == strcmp(argv[0], "fg")) {
		// If bg or fg
		#ifdef DEBUG
			printf("\033[1;30mNo.%d-----------------------bgfg------------------------\033[0m\n", ++STEPINDEX);
		#endif
		
		do_bgfg(argv);
		return 1;
	}

/* ■■■■                                                                 ■■■■■■ */
/* ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■ */
  return 0;     /* not a builtin command */
}

/*
 * do_bgfg - Execute the builtin bg and fg commands
 */
void do_bgfg(char **argv)
{
/* ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■ *
 * do bg or fg.
 * ■■■■                                                                 ■■■■■■ */
	
	int jid;
	pid_t pid;
	struct job_t *job;

	//error handdling, check arguments exist
	if (NULL == argv[1]) {
		printf("%s command requires PID or %%jobid argument\n", argv[0]);
		return;
	}
	// check first char valid
	if ('%' != argv[1][0] && (argv[1][0] > '9' || argv[1][0] < '0')) {
		printf("%s: argument must be a pid or %%jobid\n", argv[0]);
		return;
	}

	if ('%' == argv[1][0]) {
		// If search by jid
		jid = atoi(argv[1]+1);
		job = getjobjid(jobs, jid);

		if (NULL == job) {
			printf("%s: No such job\n", argv[1]);
			return;
		}

		pid = jobs[jid - 1].pid;
	} else {
		// else by pid
		int index = 0;

		char *str;
		str = (char *) malloc ((strlen(argv[1])+1) * sizeof(char));
		str[0] = '\0';
		while ('\0' != argv[1][index] && (argv[1][index] >= '0' && argv[1][index] <= '9')) {
			str[index] = argv[1][index];
			++index;
		}

		str[index] = '\0';
		pid = atoi(str);
		jid = pid2jid(pid);
		job = getjobpid(jobs, pid);

		if (NULL == job) {
			printf("(%s): No such process\n", str);
			return;
		}
	}

	#ifdef DEBUG
		printf("\033[1;30mNo.%d---------------bg or fg----------------------------\033[0m\n", ++STEPINDEX);
 		// // printf("<<< \033[1;32m%s\033[0m\n", cmdline);
		fflush(stdout);
	#endif

	// deal bg gf, send SIGCONT.
	if (0 == strcmp(argv[0], "bg")) {
		if (kill(-pid, SIGCONT) < 0) {
			fprintf(stdout, "\033[0;32;31mkill error in SIGCONT to bg\033[0m\n");
			return;
		}
		jobs[jid-1].state = BG;
		printf("[%d] (%d) %s", jid, pid, jobs[jid-1].cmdline);
	} else { 	/* fg */
		if (kill(-pid, SIGCONT) < 0) {
			fprintf(stdout, "\033[0;32;31mkill error in SIGCONT to fg\033[0m\n");
			return;
		}
		jobs[jid-1].state = FG;
		waitfg(pid);
	}
/* ■■■■                                                                 ■■■■■■ */
/* ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■ */

  return;
}

/*
 * waitfg - Block until process pid is no longer the foreground process
 */
void waitfg(pid_t pid)
{
/* ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■ *
 * built in command
 * ■■■■                                                                 ■■■■■■ */

	#ifdef DEBUG
		pid_t foregroundPid;
		foregroundPid = fgpid(jobs);
		printf("\033[1;30m fgpid: %d, argpid: %d \033[0m\n", foregroundPid, pid);
	#endif

	// 1. Use pause handle the wait.
	getIntOrStp = 0;
	// pause();
	// if (!getIntOrStp) {
	// 	#ifdef DEBUG
	// 		printf("pause 1 more time for ctrl+z\n");
	// 	#endif
	// 	pause();
	// }

	// 2. Use busy loop handle the wait
	while (pid == fgpid(jobs)){
        sleep(0);
    }

/* ■■■■                                                                 ■■■■■■ */
/* ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■ */
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
void sigchld_handler(int sig)
{
/* ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■ *
 * Signal Child Handler
 * ■■■■                                                                 ■■■■■■ */
 	
	int status;
	pid_t pid;
	int jid;
	pid_t foregroundPid;
	foregroundPid = fgpid(jobs);
	#ifdef DEBUG
		pid_t temppid;
		printf("\033[1;31m{SIGCHLD}\033[0m");
	#endif

	getIntOrStp = 0;
	
	while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0) {
	// while ((pid = testWait()) > 0) {

		#ifdef DEBUG
			temppid = pid;
			printf("\033[1;30mBefore delete get pid: %d fgpid: %d \033[0m\n", pid, foregroundPid);
		#endif
		
		jid = pid2jid(pid);
		if (FG == jobs[jid-1].state) {
			getIntOrStp = 1;
		}
		
		if (WIFSTOPPED(status)) {
			printf("Job [%d] (%d) stopped by signal %d\n", pid2jid(pid), pid, WSTOPSIG(status));
			fflush(stdout);

			jobs[jid-1].state = ST;
		} else if (WIFSIGNALED(status)) {
			printf("Job [%d] (%d) terminated by signal %d\n", pid2jid(pid), pid, SIGINT);
			fflush(stdout);

			deletejob(jobs, foregroundPid);
		} else {
			deletejob(jobs, foregroundPid);
		}
	
		#ifdef DEBUG
			printf("\033[0;32;34mDelete pid:\033[0m %d \n", temppid);
			printf("\033[1;33m[SIGCHLD]A Child finished and told his father [%d] \033[0m \n", sig);
			fflush(stdout);
		#endif
	}
	
/* ■■■■                                                                 ■■■■■■ */
/* ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■ */
  return;
}

/*
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.
 */
void sigint_handler(int sig)
{
/* ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■ *
 * Signal Interrupt Handler
 * ■■■■                                                                 ■■■■■■ */
 	
	pid_t pid;
	pid = fgpid(jobs);
	
	#ifdef DEBUG
		int jid;
		jid = pid2jid(pid);
		printf("\033[1;33m[SIGINT]You pressed ctrl+c, \033[0m \n");
		printf("\033[1;30mNo.%d---------------------------------------------------\033[0m\n", ++STEPINDEX);
		printf("\033[1;36mpid: %d jid: %d in interrupt handdler \033[0m\n", pid, jid);
		fflush(stdout);
	#endif
	// getIntOrStp = 1;
	if (kill(-pid, SIGINT) < 0) {
		fprintf(stdout, "\033[0;32;31mkill (%d) error in SIGINT\033[0m\n", pid);
		return;
	}
 	// printf("!Job [%d] (%d) terminated by signal %d\n", jid, pid, sig);

/* ■■■■                                                                 ■■■■■■ */
/* ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■ */

  return;
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.
 */
void sigtstp_handler(int sig)
{
/* ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■ *
 * Signal stop Handler
 * ■■■■                                                                 ■■■■■■ */
 	pid_t pid;
	pid = fgpid(jobs);
	
	if (kill(-pid, SIGTSTP) < 0) {
		fprintf(stdout, "\033[0;32;31mkill error in SIGSTSP\033[0m\n");
		return;
	}
	int jid;
	jid = pid2jid(pid);
	jobs[jid-1].state = ST;

	getIntOrStp = 1;
	
	#ifdef DEBUG
		printf("\033[1;30mNo.%d---------------------------------------------------\033[0m\n", ++STEPINDEX);
		printf("\033[1;33m[SIGSTP]You pressed ctrl+z, \033[0m\n");
		printf("\033[1;36mpid: %d jid: %d in stop handdler \033[0m\n", pid, jid);
		fflush(stdout);
	#endif
	// printf("!Job [%d] (%d) stopped by signal %d\n", jid, pid, sig);

/* ■■■■                                                                 ■■■■■■ */
/* ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■ */
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



/* ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■ *
 * Custom function implementation
 * ■■■■                                                                 ■■■■■■ */
  
pid_t testWait() {
	pid_t xx = waitpid(-1, NULL, WNOHANG | WUNTRACED);
	// printf("{%d}\n", xx);
	return xx;
}

/* ■■■■                                                                 ■■■■■■ */
/* ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■ */