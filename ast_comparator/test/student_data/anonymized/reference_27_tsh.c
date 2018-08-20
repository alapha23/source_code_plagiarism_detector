/* 
 * tsh - A tiny shell program with job control
 * 
 * Andrew Wang - wang62
 * Michael Zochowski - zochowsk
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

/*Zoch's stuff */
#define SIGINT 2
#define SIGSTP 19
#define SIGCHLD 17

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

/* Our helper functions */
pid_t Fork(void);
pid_t Getpgid(pid_t pid);
int Sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
int Sigemptyset(sigset_t *set);
int Sigaddset(sigset_t *set, int signo);
int Setpgid(pid_t pid, pid_t pgid);
int Kill(pid_t pid, int signum);
int Waitpid(pid_t pid, int *status, int options);

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
	char *arguments[MAXLINE];
	//1 if its in bg or >=0 arguments
	//0 if its not in the bg
	int bg = parseline(cmdline, arguments);

	// if command line empty return
	if (arguments == NULL)
		return;
	
	// 0 only if its not a built in argument
	if(builtin_cmd(arguments) == 0){
	
		//it must be an executable
		pid_t child_id;
		
		//sigprocmask block child
		sigset_t child_set;
		Sigemptyset(&child_set);
		Sigaddset(&child_set,SIGCHLD);
		
		if (Sigprocmask(SIG_BLOCK, &child_set, NULL) == -1)
			return;
		
		if((child_id = Fork()) == 0)
		{
			//unblock childsignal
			if (Sigprocmask(SIG_UNBLOCK, &child_set, NULL)==-1)
				unix_error("Fail to unblock child block signal");
			
			//change the pgid of that process to its own pid
			Setpgid(0, 0);
			
			// run the stuff and check for error
			if(execve(arguments[0],arguments,environ) == -1)
			{
				// print error if no command found
				if (errno == ENOTDIR || errno == ENOENT)
				{
					printf("%s: Command not found\n", arguments[0]);
					exit(0);
				}
			}
		}
		
		//add to jobs and deal with it being fg or not
		if(bg) {
			addjob(jobs, child_id, BG, cmdline);
			printf("[%d] (%d) %s",pid2jid(child_id),child_id,cmdline);
			if(Sigprocmask(SIG_UNBLOCK, &child_set, NULL) == -1)
				unix_error("Failure to unblock child signals");
		}
		else {
			addjob(jobs, child_id, FG, cmdline);
			if(Sigprocmask(SIG_UNBLOCK, &child_set, NULL)==-1)
				unix_error("Failure to unblock child signals");
			waitfg(child_id);	
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
	// check each allowed command with strcmp
	// quit
	if (argv != NULL && strcmp("quit", argv[0]) == 0) {
		//send interrupt signal to all processes
		exit(0);
	}

	// fg and bg command
	if (argv != NULL && (strcmp("fg",argv[0]) == 0 || strcmp("bg", argv[0]) == 0)) {
		do_bgfg(argv);
		return 1;
	}

	//jobs command
	if (argv!=NULL && strcmp("jobs",argv[0])==0) {
		listjobs(jobs);
		return 1;
	}

	// not a built-in command, return 0
	return 0;
}

/* 
 * do_bgfg - Execute the builtin bg and fg commands
 */
void do_bgfg(char **argv) 
{
	// return if no first argument
	if (argv != NULL)
	{
		// determine if argument is for fg or bg
		char *cmd;
		int cmd_type = strcmp(argv[0], "fg");
		if (cmd_type == 0)
			cmd = "fg";
		else
			cmd = "bg";

		// return if no second argument with error
		if (argv[1] == NULL)
		{
			printf("%s command requires PID or %%jobid argument\n", cmd);
			return;
		}

		// locals to store arguments
		struct job_t* job = NULL;
		// initialize buffer for strtol
		static char arguments[MAXLINE];
		char *buffer = arguments;
		buffer[0] = '\0';

		/* NOTE: The fg <job> command restarts <job> by sending it a SIGCONT signal, and then runs it in
		the foreground. The <job> argument can be either a PID or a JID */
		
		// get job if argument is jid (starts with %)
		if (argv[1][0] == '%')
		{
			// convert argument to integer (advance over %)
			int jid = (int) (strtol(++argv[1], &buffer, 10));

			// check for valid argument
			if ((jid == 0 && buffer[0]) || jid < 0)
			{
				printf("%s: argument must be PID or %%jobid\n", cmd);
				return;
			}

			// acquire job struct from jid
			job = getjobjid(jobs, jid);
			if (job == NULL)
			{
				printf("%%%d: No such job\n", jid);
				return;
			}
		}

		// get job if argument is pid
		else {
			// convert argument to pid
			int pid = (int)(strtol(argv[1], &buffer, 10));

			// check for valid argument
			if ((pid == 0 && buffer[0]) || pid < 0)
			{
				printf("%s: argument must be PID or %%jobid\n", cmd);
				return;
			}

			// acquire job struct from pid
			job = getjobpid(jobs, pid);
			if (job == NULL)
			{
				printf("(%d): No such process\n", pid);
				return;
			}
		}
		
		//pid should be the group_id, so signal all the processes if that group to continue	
		// fg command
		if (cmd_type == 0)
		{
			// make sure process is stopped or in background
			if (job->state == BG || job->state == ST)
			{
				// continue group
				Kill((-1)*(job->pid), SIGCONT);
				// change status
				job->state = FG;
				// waitfg
				waitfg(job->pid);
			}
		}
		
		// bg command
		else
		{
			// only change if stopped
			if (job->state == ST)
			{
				Kill((-1)*(job->pid), SIGCONT);
				job->state = BG;
				// retrieve command line from job
				char *cmd_ptr = (char *)&(job->cmdline);
				// display move to background
				printf("[%d] (%d) %s", job->jid, job->pid, cmd_ptr);
			}
		}
	}
    return;
}


/* 
 * waitfg - Block until process pid is no longer the foreground process
 */
void waitfg(pid_t pid)
{
	struct job_t *job;
	// loop as long as process is in the foreground 
	while((job = getjobpid(jobs,pid)) != NULL) {
		if(job->state == ST)
			break;
		if(job->state == BG)
			break;
		else
			// wait for 1 second
			sleep(1);
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
	pid_t pid;

	// loop for Waitpid
	while((pid=Waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0)
	{
		// reap return values
		struct job_t *job = getjobpid(jobs,pid);
		
		if(WIFSIGNALED(status)!=0){  
			deletejob(jobs,pid);			
		}
		
		else if(WIFSTOPPED(status)!=0){
			job->state = ST;
		}
		
		else if(WIFEXITED(status)!=0){	
			deletejob(jobs,pid);
		}
		
		else
			printf("incorrectly reached end of sigchld hanler loop");
	}
}

/*
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.  
 */
void sigint_handler(int sig)
{
	pid_t fg_pid = fgpid(jobs);
	
	//check if it worked	
	struct job_t *fgjob = getjobpid(jobs,fg_pid);
	
	//check again	
	if (Kill((-1)*(fg_pid),sig) == -1)
		return;
	
	//say you killed it
	printf("Job [%d] (%d) terminated by signal %d\n",fgjob->jid,fg_pid,sig);
	
	return;
}
 
/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.  
 */
void sigtstp_handler(int sig)
{
	pid_t fg_pid = fgpid(jobs);
	
	//check if it worked
	
	struct job_t *fgjob = getjobpid(jobs,fg_pid);
	
	//check again
	
	if(Kill((-1)*(fg_pid),sig)==-1)
		return;
	
	//say you killed it
	printf("Job [%d] (%d) stopped by signal %d\n",fgjob->jid,fg_pid,sig);
	
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
	    if (nextjid > MAXJID)
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
    Sigemptyset(&action.sa_mask); /* block sigs of type being handled */
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


/*****
 * Wrappers to check system calls
 *****/

/*
 * Fork - wrapper function for fork that checks return status
 */
pid_t Fork(void)
{
	pid_t pid;
	if ((pid = fork()) == -1)
		    unix_error("Fork error");
	return pid;
}

/*
 * GetPGID - wrapper function for getpgid
 */
pid_t Getpgid(pid_t pid)
{
	// check return of getpgid
	pid_t pgid;
	if ((pgid = getpgid(pid)) == -1)
		unix_error("getpgid error");
	return pgid;
}

/*
 * Sigprocmask - wrapper function for sigprocmask checking for return status
 */
int Sigprocmask(int how, const sigset_t *set, sigset_t *oldset)
{
	int ret_val;
	if((ret_val=sigprocmask(how,set,oldset)) == -1)
		unix_error("sigprocmask error");
	return ret_val;
}


/*
 * Sigemptyset - wrapper function for sigemptyset checking for return status
 */
int Sigemptyset(sigset_t *set)
{
	int ret_val;
	if((ret_val=sigemptyset(set)) == -1)
		unix_error("sigemptyset error");
	return ret_val;
}


/*
 * Sigaddset - wrapper function for sigaddset checking for return status
 */
int Sigaddset(sigset_t *set, int signo)
{
	int ret_val;
	if((ret_val=sigaddset(set, signo)) == -1)
		unix_error("sigaddset error");
	return ret_val;
}


/*
 * Setpgid - wrapper function for setpgid
 */
int Setpgid(pid_t pid, pid_t pgid)
{
	// check return of setpgid
	int ret_val;
	if ((ret_val = setpgid(pid, pgid)) == -1)
		unix_error("eval: setpgid error");
	return ret_val;
}


/*
 * Kill - wrapper function for kill
 */
int Kill(pid_t pid, int signum)
{
	// check kill return value
    int ret_val;
    if((ret_val = kill(pid, signum)) == -1)
    		unix_error("Signal error");
    return ret_val;
}

/*
 * Waitpid - wrapper for waitpid
 */
int Waitpid(pid_t pid, int *status, int options)
{
	/* NOTE: We have to care about whether or not waitpid returns -1 or 0.
		0 shouldn't actually exist because we have both WNOHANG and WUNTRACED
		which requires the stuff returned to be stopped. -1 happens if error, 
		so we have to check what errno is set to. ECHILD is if no child and 
		EINTR is if we are interuptted by somethnig else */

	errno = 0;
	pid_t ret_val = -1;
	// only return error if it doesn't result from no children (ECHILD)
	if ((ret_val = waitpid(pid, status, options)) == -1 && errno != ECHILD)
	{
		unix_error("waitpid error");
	}
	return ret_val;
}



