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

int is_number(char *input); // check whether input is number or not. used in do_bgfg

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
	char *argv[MAXARGS]={0}; // an array to store parsed arguments.
	int isBG = parseline(cmdline,argv); // parse commandline and store parsed arguments in argv. 
										// and return value of parseline(inform that whether it is bg request or not) is stored in isBG.
	pid_t c_pid;
	sigset_t sigset;                    
	if(sigemptyset(&sigset)==-1)					//
	{												//
		unix_error("eval: sigemptyset failure");	//
	}												// prepare mask set to block SIGCHLD
	if(sigaddset(&sigset,SIGCHLD)==-1)				//
	{												//
		unix_error("eval: sigaddset failure");		//
	}												//
	if(argv[0]==NULL) // check illegal input cmdline.
	{
		return;
	}
	if(!(builtin_cmd(argv))) // if it is builtin cmd request, immediately carry out the request and return to main function.
	{						 // if it is not builtin cmd request, operate fork and execve.
		if(sigprocmask(SIG_BLOCK,&sigset,NULL)==-1)          // before it fork, block SIGCHLD to avoid race condition.
		{													 
			unix_error("eval: sigprocmask failure");		 
		}													 
		c_pid = fork();										// fork to execute requested program. 
		if(c_pid == -1) // error
		{
			unix_error("eval: fork failure");
		}
		else if(c_pid == 0) // child						
		{
			if(sigprocmask(SIG_UNBLOCK,&sigset,NULL)==-1)	// after it fork, in child,
			{												// since children inherit the blocked vectors of their parents, unblock SIGCHLD.
				unix_error("eval: sigprocmask failure");    
			}															
			if(setpgid(0,0)==-1)							// put the child in a new process group to ensure that 
			{											    // there will be only one process, my tiny shell, in the foreground process group.
				unix_error("eval: setpgid failure");		
			}												
			if(execve(argv[0],argv,environ)==-1)			// execute requested program.
			{
				printf("%s: Command not found\n",argv[0]);  // if there is no such program, print message.
				exit(0);
			}
		}
		else // parent
		{
			if(isBG) // BG request
			{																// BG case
				addjob(jobs,c_pid,BG,cmdline);							 	// 1. add new child job to the job list.
				printf("[%d] (%d) %s",pid2jid(c_pid),c_pid,cmdline);		// 2. print message.
				if(sigprocmask(SIG_UNBLOCK,&sigset,NULL)==-1)				// 3. unblock SIGCHLD.(it's okay because shell done adding new job already.)
				{
					unix_error("eval: sigprocmask failure");
				}
			}
			else // FG request
			{																// FG case
				addjob(jobs,c_pid,FG,cmdline);								// 1. add new child job to the job list.
				if(sigprocmask(SIG_UNBLOCK,&sigset,NULL)==-1)				// 2. unblock SIGCHLD.(it's okay because shell done adding new job already.)
				{															//
					unix_error("eval: sigprocmask failure");				//
				}															//
				waitfg(c_pid);												// 3. call waitfg to wait fg_job.
			}
		}	
	}
	
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
	if(strcmp(argv[0],"quit")==0)		// quit : simply call exit
	{
		exit(0);
	}
	else if(strcmp(argv[0],"jobs")==0)	// jobs : call listjobs()
	{
		listjobs(jobs);
		return 1;
	}
	else if(strcmp(argv[0],"bg")==0)	// bg and fg : call do_bgfg()
	{
		do_bgfg(argv);
		return 1;
	}
	else if(strcmp(argv[0],"fg")==0)
	{
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
	int id;

// ------------------------- check input validness and get the job from joblist ----------------------------------------

	if(argv[1]==NULL) // if there is no argument of bg,fg
	{
		printf("%s command requires PID or %%jobid argument\n",argv[0]);
		return;
	}
	if(argv[1][0]=='%') // jid case
	{
		if(is_number(&argv[1][1])) 
		{								// if it is valid input
			id = atoi(&argv[1][1]);		// get jid(integer)
			job = getjobjid(jobs,id);	// get job from jid
			if(job == NULL) // if there is no such job
			{
				printf("%s: No such job\n",argv[1]);
				return;
			}
		}
		else // if argument is not a number
		{
			printf("%s: argument must be a PID or %%jobid\n",argv[0]);
			return;
		}
	}
	else // pid case
	{
		if(is_number(argv[1]))
		{								// if it is valid input
			id = atoi(argv[1]);			// get pid(integer)
			job = getjobpid(jobs,id);	// get job from pid
			if(job == NULL) // if there is no such process
			{
				printf("(%d): No such process\n",id);
				return;
			}
		}
		else // if argument is not a number
		{
			printf("%s: argument must be a PID or %%jobid\n",argv[0]);
			return;
		}
	}
//--------------------------------------------------------------------------------------------------------------------	

	if(strcmp(argv[0],"bg")==0) // built in command bg
	{
		job->state = BG;											// change state of the job
		if(kill(-(job->pid),SIGCONT)==-1)							// send SIGCONT to the job(entire process group).
		{
			unix_error("do_bgfg: kill failure");
		}
		printf("[%d] (%d) %s",job->jid,job->pid,job->cmdline);		// print proper message.
	}
	else // built in command fg
	{
		job->state = FG;					// change state of the job
		if(kill(-(job->pid),SIGCONT)==-1)	// send SIGCONT to the job(entire process group).
		{
			unix_error("do_bgfg: kill failure");
		}
		waitfg(job->pid);					// and wait the job(now foreground job).
	}
  return;
}
/*
 * is_number - check whether input string is number or not (return 0 : it is not a number (ex:"123a35d"))
 														   (return 1 : it is a number)
 */
int is_number(char *input)
{
	int i;
	for(i=0;input[i]!='\0';i++) // check every single characters until end of string.
	{
		if(!((input[i]>=48)&&(input[i]<=57)))
		{
			return 0;
		}
	}
	return 1;
}

/*
 * waitfg - Block until process pid is no longer the foreground process
 */
void waitfg(pid_t pid)
{
	while(1) 	
	{
		if(fgpid(jobs) != pid) // if waiting process is no longer fgjob, stop waiting.
		{
			break;
		}
		sleep(1000); // sleep until some signal(mostly SIGCHLD) arrives. (if some signal arrived, sleep returns immediately.)
					 // I think that this sleep make program more peaceful.(but program may work without this.)
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
void sigchld_handler(int sig)
{
	int status;
	struct job_t *child_job;
	pid_t child_pid;
	while(1)
	{
		child_pid = waitpid(-1,&status,WUNTRACED|WCONTINUED|WNOHANG); // reap zombies and catch stopped or continued child.
		if(child_pid == -1) // waitpid failure case - there is no more child
		{
			break;
		}
		if(child_pid == 0) // there is no more child with changed state. (WNOHANG case)
		{
			break;
		}
		child_job = getjobpid(jobs, child_pid); // get job_t of child.
		if(WIFSTOPPED(status)) // signal with stopped child (WUNTRACED case)
		{
			child_job->state = ST; // change state of stopped child.
			printf("Job [%d] (%d) stopped by signal %d\n",pid2jid(child_pid),child_pid,WSTOPSIG(status)); // print proper message.
		}
		else if(WIFCONTINUED(status)) // signal with continued child (WCONTINUED case)
		{ 							  // nothing to do here.
		}
		else // signal with terminated child
		{
			if(WIFSIGNALED(status)) // if child killed by some signal
			{
				printf("Job [%d] (%d) terminated by signal %d\n",pid2jid(child_pid),child_pid,WTERMSIG(status)); // print terminating signal info
			}
			deletejob(jobs,child_pid); // delete terminated child from job list.
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
	pid_t pid = fgpid(jobs); // get fgpid
	if(pid != 0)			 // if there is a foreground job
	{
		if(kill(-pid,SIGINT)==-1)	// send SIGINT to the foreground job.(entire foreground process group.)
		{
			unix_error("sigint_handler: kill failure");
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
	pid_t pid = fgpid(jobs); // get fgpid
	if(pid != 0)  			 // if there is a foreground job
	{
		if(kill(-pid,SIGTSTP)==-1) // send SIGTSTP to foreground job.(entire foreground process group.)
		{
			unix_error("sigtstp_handler: kill failure");
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



