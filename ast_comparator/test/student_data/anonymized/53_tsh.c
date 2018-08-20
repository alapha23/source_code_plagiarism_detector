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
#define STATE_UNDEFINED  0  /* undefined */
#define STATE_FOREGROUND 1  /* running in foreground */
#define STATE_BACKGROUND 2  /* running in background */
#define STATE_STOPPED    3  /* stopped */

/*
 * Jobs states: FG (foreground), BG (background), ST (stopped)
 * Job state transitions and enabling actions:
 *     FG -> ST  : ctrl-z
 *     ST -> FG  : fg command
 *     ST -> BG  : bg command
 *     BG -> FG  : fg command
 * At most 1 job can be in the FG state.
 */

/* Bool type & constants definitions */
#ifndef __cplusplus
typedef char bool;
#define true 1
#define false 0
#endif
/* End Bool type & constants definitions*/

/* Global variables */
extern char **environ;         /* defined in libc */
const char prompt[] = "tsh> "; /* command line prompt (DO NOT CHANGE) */
int verbose = 0;               /* if true, print additional output */
int nextjid = 1;               /* next job ID to allocate */
char sbuf[MAXLINE];            /* for composing sprintf messages */

struct job_t                   /* The job struct */
{
	pid_t pid;                 /* job PID */
	int jid;                   /* job ID [1, 2, ...] */
	int state;                 /* UNDEFINED, BG, FG, or ST */
	char cmdline[MAXLINE];     /* command line */
};
struct job_t jobs[MAXJOBS];    /* The job list */
const sigset_t sigchild;       /* sigset_t which only contains sigchild */
/* End global variables */


/* Function prototypes */

/*----------------------------------------------------------------------------
 * Functions that you will implement
 */

void eval(const char *cmdline);
bool try_execute_builtin_command(char **argv);
void do_bgfg(char **argv);
void wait_fg_process(pid_t pid);

void sigchld_handler(int sig);
void sigint_handler(int sig);
void sigtstp_handler(int sig);

/*----------------------------------------------------------------------------*/

/* These functions are already implemented for your convenience */
void parse_line(const char *cmdline, char **argv, bool *bg);
void sigquit_handler(int sig);

void clear_job_struct(struct job_t *job);
void initialize_job_list(struct job_t *jobs);
int get_max_jid(const struct job_t *jobs);
struct job_t *add_job(struct job_t *jobs, pid_t pid, int state, const char *cmdline);
bool delete_job_by_pid(struct job_t *jobs, pid_t pid);
void delete_job_by_job_t(struct job_t *jobs, struct job_t *job);
pid_t get_current_fg_pid(const struct job_t *jobs);
struct job_t *get_job_by_pid(const struct job_t *jobs, pid_t pid);
struct job_t *get_job_by_jid(const struct job_t *jobs, int jid);
int pid_to_jid(pid_t pid);
void print_job_list(const struct job_t *jobs);

void usage(void);
void unix_error(const char *msg);
void app_error(const char *msg);
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
	while ((c = getopt(argc, argv, "hvp")) != EOF)
	{
		switch (c)
		{
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
	Signal(SIGINT, sigint_handler);   /* ctrl-c */
	Signal(SIGTSTP, sigtstp_handler);  /* ctrl-z */
	Signal(SIGCHLD, sigchld_handler);  /* Terminated or stopped child */

	/* This one provides a clean way to kill the shell */
	Signal(SIGQUIT, sigquit_handler);

	/* Initialize the job list */
	initialize_job_list(jobs);
	
	/* Initialize global variable sigchild */
	sigset_t *sigchild_pointer = (sigset_t *) &sigchild;
	if (sigemptyset(sigchild_pointer) == -1)
		unix_error("sigemptyset failed");
	if (sigaddset(sigchild_pointer, SIGCHLD) == -1)
		unix_error("sigaddset failed");
	
	/* Execute the shell's read/eval loop */
	while (1)
	{

		/* Read command line */
		if (emit_prompt)
		{
			printf("%s", prompt);
			fflush(stdout);
		}
		if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
			app_error("fgets error");
		if (feof(stdin))
		{ /* End of file (ctrl-d) */
			fflush(stdout);
			exit(0);
		}

		/* Evaluate the command line */
		eval(cmdline);
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
void eval(const char *cmdline)
{
	bool background;
	char *argv[MAXARGS];
	pid_t child_pid;
	
	parse_line(cmdline, argv, &background);
	
	if (!try_execute_builtin_command(argv))
	{
		if (sigprocmask(SIG_BLOCK, &sigchild, NULL) == -1)
			unix_error("Blocking SIGCHLD failed");
		child_pid = fork();
		
		if (child_pid == -1)
			unix_error("fork failed");
		else if (child_pid == 0)
		{
			if (setpgid(0, 0) == -1)
				unix_error("setpgid failed");
			if (sigprocmask(SIG_UNBLOCK, &sigchild, NULL) == -1)
				unix_error("Unblocking SIGCHLD failed");
			execvp(argv[0], argv);
			
			// If exec fails,
			if (errno == ENOENT)
				fprintf(stderr, "%s: Command not found\n", argv[0]);
			else
				unix_error(argv[0]);
			exit(0);
		}
		else
		{
			if (background)
			{
				struct job_t *job = add_job(jobs, child_pid, STATE_BACKGROUND, cmdline);
				if (job != NULL)
					printf("[%d] (%d) %s", job->jid, job->pid, job->cmdline);
				if (sigprocmask(SIG_UNBLOCK, &sigchild, NULL) == -1)
					unix_error("Unblocking SIGCHLD failed");
			}
			else
			{
				add_job(jobs, child_pid, STATE_FOREGROUND, cmdline);
				if (sigprocmask(SIG_UNBLOCK, &sigchild, NULL) == -1)
					unix_error("Unblocking SIGCHLD failed");
				wait_fg_process(child_pid);
			}
		}
	}
}

/*
 * parse_line - Parse the command line and build the argv array.
 *
 * Characters enclosed in single quotes are treated as a single
 * argument.  Return true if the user has requested a BG job, false if
 * the user has requested a FG job.
 */
void parse_line(const char *cmdline, char **argv, bool *bg)
{
	static char array[MAXLINE]; /* holds local copy of command line */
	char *buf = array;          /* ptr that traverses command line */
	char *delim;                /* points to first space delimiter */
	int argc;                   /* number of args */
	bool background;            /* background job? */
	
	if (argv == NULL)
		return;
	
	strcpy(buf, cmdline);
	buf[strlen(buf) - 1] = ' '; /* replace trailing '\n' with space */
	while (*buf && (*buf == ' ')) /* ignore leading spaces */
		buf++;

	/* Build the argv list */
	argc = 0;
	if (*buf == '\'')
	{
		buf++;
		delim = strchr(buf, '\'');
	}
	else
	{
		delim = strchr(buf, ' ');
	}

	while (delim)
	{
		argv[argc++] = buf;
		*delim = '\0';
		buf = delim + 1;
		while (*buf && (*buf == ' ')) /* ignore spaces */
			buf++;

		if (*buf == '\'')
		{
			buf++;
			delim = strchr(buf, '\'');
		}
		else
		{
			delim = strchr(buf, ' ');
		}

		if (argc >= MAXARGS) /* to prevent buffer overflow */
			break;
	}
	argv[argc] = NULL;

	if (argc == 0)  /* ignore blank line */
		background = true;
	/* should the job run in the background? */
	else if ((background = (*argv[argc - 1] == '&')))
	{
		argv[--argc] = NULL;
	}
	
	if (bg != NULL)
		*bg = background;
}

/*
 * try_execute_builtin_command - If the user has typed a built-in 
 *    command then execute it immediately.
 * Returns true if given command is built-in command, false otherwise.
 */
bool try_execute_builtin_command(char **argv)
{
	if (argv == NULL || argv[0] == NULL)
		return false; // Goto normal execution routine to print error message
	
	if (strcmp(argv[0], "quit") == 0)
	{
		/*  Built-in commands are not executed in child process.
		 * This will exit shell process directly. */
		exit(0);
	}
	else if (strcmp(argv[0], "fg") == 0 || strcmp(argv[0], "bg") == 0)
	{
		do_bgfg(argv);
		return true;
	}
	else if (strcmp(argv[0], "jobs") == 0)
	{
		print_job_list(jobs);
		return true;
	}
	else
		return false;     /* not a builtin command */
}

/*
 * do_bgfg - Execute the builtin bg and fg commands
 */
void do_bgfg(char **argv)
{
	bool background, argument_error = false;
	struct job_t *target;
	int jid;
	pid_t pid;
	
	if (argv == NULL || argv[0] == NULL)
		return;
	
	if (strcmp(argv[0], "bg") == 0)
		background = true;
	else if (strcmp(argv[0], "fg") == 0)
		background = false;
	else
		return;
	
	const char * const command = (background? "bg" : "fg");
	
	if (argv[1] == NULL)
	{
		printf("%s command requires PID or %%jobid argument\n", command);
		return;
	}
	else
	{
		char *endptr;
		
		errno = 0; // Reset errno for strtol
		if (argv[1][0] == '%')
		{
			jid = strtol(&argv[1][1], &endptr, 10);
			if (&argv[1][1] == endptr || errno == ERANGE || errno == EINVAL)
				argument_error = true;
			else
				target = get_job_by_jid(jobs, jid);
		}
		else
		{
			pid = strtol(&argv[1][0], &endptr, 10);
			if (&argv[1][0] == endptr || errno == ERANGE || errno == EINVAL)
				argument_error = true;
			else
				target = get_job_by_pid(jobs, pid);
		}
	}
	
	if (argument_error)
	{
		printf("%s: argument must be a PID or %%jobid\n", command);
	}
	else if (target == NULL)
	{
		if (argv[1][0] == '%')
			printf("%%%d: No such job\n", jid);
		else
			printf("(%d): No such process\n", pid);
	}
	else
	{
		if (kill(-target->pid, SIGCONT) == -1)
			unix_error("Sending SIGCONT failed");
		
		if (background)
		{
			printf("[%d] (%d) %s", target->jid, target->pid, target->cmdline);
			target->state = STATE_BACKGROUND;
		}
		else
		{
			target->state = STATE_FOREGROUND;
			wait_fg_process(target->pid);
		}
	}
}

/*
 * wait_fg_process - Block until process pid is no longer the foreground process
 */
void wait_fg_process(pid_t pid)
{
	struct job_t *job = get_job_by_pid(jobs, pid);
	
	/* If job is terminated, state would have been changed to undefined
	 * by delete_job function */
	while (job->state == STATE_FOREGROUND)
	{
		if (pause() != -1)
			unix_error("pause() returned unexpected value");
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
	pid_t child_pid;
	int child_status;
	
	while (true)
	{
		child_pid = waitpid(-1, &child_status, WNOHANG | WUNTRACED | WCONTINUED);
		if (child_pid == 0)
			break;
		else if (child_pid == -1)
		{
			if (errno == ECHILD)
				break;
			else
				unix_error("waitpid failed");
		}
		
		struct job_t *job = get_job_by_pid(jobs, child_pid);
		
		if (WIFEXITED(child_status)) // Child is exited
			delete_job_by_job_t(jobs, job);
		else if (WIFSIGNALED(child_status)) // Child is terminated by signal
		{
			printf("Job [%d] (%d) terminated by signal %d\n", job->jid, job->pid, WTERMSIG(child_status));
			delete_job_by_job_t(jobs, job);
		}
		else if (WIFSTOPPED(child_status)) // Child is stopped
		{
			printf("Job [%d] (%d) stopped by signal %d\n", job->jid, job->pid, WSTOPSIG(child_status));
			job->state = STATE_STOPPED;
		}
		else if (WIFCONTINUED(child_status)) // Child is continued
		{
			// Do nothing if child is continued by fg command
			if (job->state == STATE_FOREGROUND)
				continue;
			// If child is continued by bg command or signal from outside
			else
				job->state = STATE_BACKGROUND;
		}
		else
		{
			fprintf(stderr, "Untreated status %d from child %d\n", child_status, child_pid);
			print_job_list(jobs);
			exit(1);
		}
	}
}

/*
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.
 */
void sigint_handler(int sig)
{
	pid_t foreground_process = get_current_fg_pid(jobs);
	if (foreground_process != 0)
		if(kill(-foreground_process, SIGINT) == -1)
			unix_error("Sending signal failed");
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.
 */
void sigtstp_handler(int sig)
{
	pid_t foreground_process = get_current_fg_pid(jobs);
	if (foreground_process != 0)
		if(kill(-foreground_process, SIGTSTP) == -1)
			unix_error("Sending signal failed");
}

/*********************
 * End signal handlers
 *********************/

 /***********************************************
  * Helper routines that manipulate the job list
  **********************************************/

  /* clear_job_struct - Clear the entries in a job struct */
void clear_job_struct(struct job_t *job)
{
	job->pid = 0;
	job->jid = 0;
	job->state = STATE_UNDEFINED;
	job->cmdline[0] = '\0';
}

/* initialize_job_list - Initialize the job list */
void initialize_job_list(struct job_t *jobs)
{
	int i;

	for (i = 0; i < MAXJOBS; i++)
		clear_job_struct(&jobs[i]);
}

/* get_max_jid - Returns largest allocated job ID */
int get_max_jid(const struct job_t *jobs)
{
	int i, max = 0;

	for (i = 0; i < MAXJOBS; i++)
		if (jobs[i].jid > max)
			max = jobs[i].jid;
	return max;
}

/* add_job - Add a job to the job list
 * Returns newly added job structure if success, NULL if not.
 */
struct job_t *add_job(struct job_t *jobs, pid_t pid, int state, const char *cmdline)
{
	int i;

	if (pid < 1)
		return 0;

	for (i = 0; i < MAXJOBS; i++)
	{
		if (jobs[i].pid == 0)
		{
			jobs[i].pid = pid;
			jobs[i].state = state;
			jobs[i].jid = nextjid++;
			if (nextjid > MAXJOBS)
				nextjid = 1;
			strcpy(jobs[i].cmdline, cmdline);
			if (verbose)
			{
				printf("Added job [%d] %d %s", jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
			}
			return &jobs[i];
		}
	}
	printf("Tried to create too many jobs\n");
	return NULL;
}

/* delete_job_by_pid - Delete a job whose PID=pid from the job list 
 * Returns true if success, false if fails 
 */
bool delete_job_by_pid(struct job_t *jobs, pid_t pid)
{
	int i;

	if (pid < 1)
		return false;

	for (i = 0; i < MAXJOBS; i++)
	{
		if (jobs[i].pid == pid)
		{
			delete_job_by_job_t(jobs, &jobs[i]);
			return true;
		}
	}
	return false;
}

/*
 * delete_job_by_job_t - Delete a job using given job pointer
 * struct job_t *jobs - 
 */
void delete_job_by_job_t(struct job_t *jobs, struct job_t *job)
{
	clear_job_struct(job);
	nextjid = get_max_jid(jobs) + 1;
}

/* get_current_fg_pid - Return PID of current foreground job, 0 if no such job */
pid_t get_current_fg_pid(const struct job_t *jobs)
{
	int i;

	for (i = 0; i < MAXJOBS; i++)
		if (jobs[i].state == STATE_FOREGROUND)
			return jobs[i].pid;
	return 0;
}

/* get_job_by_pid  - Find a job (by PID) on the job list */
struct job_t *get_job_by_pid(const struct job_t *jobs, pid_t pid)
{
	int i;

	if (pid < 1)
		return NULL;
	for (i = 0; i < MAXJOBS; i++)
		if (jobs[i].pid == pid)
			return (struct job_t *) &jobs[i];
	return NULL;
}

/* get_job_by_jid  - Find a job (by JID) on the job list */
struct job_t *get_job_by_jid(const struct job_t *jobs, int jid)
{
	int i;

	if (jid < 1)
		return NULL;
	for (i = 0; i < MAXJOBS; i++)
		if (jobs[i].jid == jid)
			return (struct job_t *) &jobs[i];
	return NULL;
}

/* pid_to_jid - Map process ID to job ID */
int pid_to_jid(pid_t pid)
{
	int i;

	if (pid < 1)
		return 0;
	for (i = 0; i < MAXJOBS; i++)
		if (jobs[i].pid == pid)
		{
			return jobs[i].jid;
		}
	return 0;
}

/* print_job_list - Print the job list */
void print_job_list(const struct job_t *jobs)
{
	int i;

	for (i = 0; i < MAXJOBS; i++)
	{
		if (jobs[i].pid != 0)
		{
			printf("[%d] (%d) ", jobs[i].jid, jobs[i].pid);
			switch (jobs[i].state)
			{
			case STATE_BACKGROUND:
				printf("Running ");
				break;
			case STATE_FOREGROUND:
				printf("Foreground ");
				break;
			case STATE_STOPPED:
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
void unix_error(const char *msg)
{
	fprintf(stdout, "%s: %s\n", msg, strerror(errno));
	exit(1);
}

/*
 * app_error - application-style error routine
 */
void app_error(const char *msg)
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
