/* 
 * tsh - A tiny shell program with job control
 * 
 * Julian Kemmerer - jvk27 - 11752442
 * Josh Henry - jdh84 - 11878175
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
    pid_t pid;			/* job PID */
    pid_t pid2;			//Second process id if pipes are used
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
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline, pid_t pid2);
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
	  //Loop through and kill all child jobs
	  //Gets stuck here why?
	  //Main turns into a zombie?
	  //Is child killing parent?
	  //int status;
	 // while(    ((waitpid(-1, &status, WNOHANG))>0)  ){}
	  //printf("here 2c\n");
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
    int bg;
    pid_t pid;
    
    bg = parseline(cmdline, argv);
    
    //Modified so bg is -1 for empty command
    if( (bg!=-1) && (!builtin_cmd(argv)) )
    {
	    //Look through argv for pipe
	    int j = 0;
	    char * pipe_str = "|";
	    int pipeindex = -1;
	    while(argv[j]!=NULL)
	    {
	      if(strncmp(pipe_str,argv[j],MAXLINE)==0)
	      {
		pipeindex = j;
		break;
	      }
	      j++;
	    }
	    
	   
	    //If pipe exists
	    //Pipe cant be first argument
	    if(pipeindex > 0)
	    {
	      int childleftpid;
	      char *argvleft[MAXARGS];
	      char *argvright[MAXARGS];
	      int childrightpid;
	      //Need to fork two children, one for each side of the pipe
	      
	      //Copy from 0 to pipeindex-1 into leftargv
	      for(j=0;j< pipeindex; j++)
	      {
		argvleft[j] = argv[j];
	      }
	      argvleft[j] = NULL;
	      
	      //Copy from pipeindex + 1 to MAXARGS
	      int newindex;
	      for(j=pipeindex+1;j<MAXARGS; j++)
	      {
		newindex = j - (pipeindex+1);
		argvright[newindex] = argv[j];
	      }
	      
      
	      //Create a pipe
	      int pipefd[2];
	      if (pipe(pipefd) < 0) {
		unix_error("Pipe error.");
	      }
	      //Read is 0, write is 1

	      //Have two new argvs
	      //Fork two children
	      
	      //Fork left child
	      if((pid = fork()) == 0)
	      {
		//Left child
		//Route stdout to write side of pipe
		dup2(pipefd[1], 1);
		
		if(execve(argvleft[0], argvleft, environ) < 0)
		{
		    printf("%s: Command not found.\n", argvleft[0]);
		    exit(0);
		}
	      }
	      else
	      {
		//Parent Still
		childleftpid = pid;
		
		//Fork right child
		if((pid = fork()) == 0)
		{
		  //Is right child
		  //Route stdin to read side of pipe
		  dup2(pipefd[0], 0);
		  if(execve(argvright[0], argvright, environ) < 0)
		  {	 
		      printf("%s: Command not found.\n", argvright[0]);
		      exit(0);
		  }
		}
		else
		{
		  //Is parent
		  childrightpid = pid;
		  
		  //Only right process in in fg
		  //Left is always in bg
		  if(!bg)
		  {
		    //Add job only once
		    addjob(jobs, childleftpid, FG, cmdline, childrightpid);
		    waitfg(childrightpid);
		  }
		  else
		  {
		    //Whole thing is in bg
		    addjob(jobs, childleftpid, BG, cmdline, childrightpid);
		  }
		}  
	      }  
	    }
	    else
	    {      
	      //No Pipes present - just one command
	      if((pid = fork()) == 0)
	      {
		//Is child
		//Set the group id to the parent id - convention
		
		if(execve(argv[0], argv, environ) < 0)
		{
			
		    printf("%s: Command not found.\n", argv[0]);
		    exit(0);
		}
	      }
	      else
	      {
		//Is parent
		//pid is child process
		if(!bg)
		{
		  //Child is in fg
		  addjob(jobs, pid, FG, cmdline,0);
		  waitfg(pid);
		}
		else
		{
		  //Child is in bg
		  addjob(jobs, pid, BG, cmdline,0);
		}
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
	return -1;

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
    //Recognizes and interprets the built-in commands: quit, fg, bg, and jobs.
    char * cmd_str = argv[0];
    char * quit_str = "quit";
    char * fg_str = "fg";
    char * bg_str = "bg";
    char * jobs_str = "jobs";
    
    //printf("cmd str = %s\n",cmd_str);
    
    //Compare the cmd with built in ones
    //Compare maximum number of chars as maxline
    if(strncmp(cmd_str,quit_str,MAXLINE)==0)
    {
      //Is quit command
      //Loop through and kill all child jobs
      int i;
      for (i = 0; i < MAXJOBS; i++) {
	  if (jobs[i].pid != 0) 
	  {
	    //Is a valid job
	    //Kill this job by sending sig term
	    kill(jobs[i].pid,SIGTERM);
	  }
      }
      //Then kill self (quit)
      exit(0);
      //Return not needed
    }
    //fg or bg
    else if( (strncmp(cmd_str,fg_str,MAXLINE)==0) || (strncmp(cmd_str,bg_str,MAXLINE)==0)) 
    {
      //Is fg or bg command
      //Run the do fg bg function
      do_bgfg(argv);
      return 1;
    }
    else if(strncmp(cmd_str,jobs_str,MAXLINE)==0)
    {
      //Is jobs command
      //Run the lsit jobs command
      listjobs(jobs);
      return 1;
    }
    else
    {
      return 0; /* not a builtin command */
    }
}
    
      

/* 
 * do_bgfg - Execute the builtin bg and fg commands
 */
void do_bgfg(char **argv) 
{
    //printf("bgfg start!\n");
  
    char * cmd_str = argv[0];
    char * bg_str = "bg";
    char * fg_str = "fg";
    
    if(argv[1] == NULL)
    {
      //No other param passed
      printf("%s command requires PID or %%jobid argument\n",argv[0]);
      return;
    }
    
    //Loop through argv[1] until null and check each char is either percent or number
    int j = 0;
    //Skip percent
    if(argv[1][0] == '%')
      j=1;
    
    
    //printf("before while!\n");
    while( argv[1][j] != NULL)
    {
     //printf("in while!\n");
      if( !( (argv[1][j] >= '0') && (argv[1][j]<='9') ) )
      {
	//printf("in if!\n");
	//Char does not fall in acceptable range
	printf("%s command requires PID or %%jobid argument\n",argv[0]);
	return;
      }
      j++;
    }
    //printf("after while!\n");
    
    //Get the first character in second arg
    char c = argv[1][0];
    char * job_str;
    int job;
    int pid;
    if(c =='%')
    {
      //Ignore the first char
      job_str = &(argv[1][1]);
      
      job = atoi(job_str); //make job the value of the int after
    }
    else
    {
      pid = atoi(argv[1]);
      struct job_t * the_job = getjobpid(jobs, pid);
      job = the_job->jid;
    }
    
    
    
    if (job < 1)
    {
	  printf("do_bgfg: invalid job");  
	  return;      
    }
    
    
    
    //Find the job in the jobs list
    int i;
    int jobindex = -1;

    for (i = 0; i < MAXJOBS; i++) 
    {
	if (jobs[i].jid == job) 
	{
	  jobindex = i;
	  break;
	}
    }
    if(jobindex <0)
    {
      printf("Could not find job in job list.");
      return;
    }
    
    //Compare the cmd with built in ones
    //Compare maximum number of chars as maxline
    if(strncmp(cmd_str,bg_str,MAXLINE)==0)
    {
      //BG
      //Set this job in background
      //Should drop out of any waitfg currently in progress
      jobs[jobindex].state = BG;
      printf("[%d] (%d) %s\n", jobs[jobindex].jid, jobs[jobindex].pid,jobs[jobindex].cmdline);
    }
    else if(strncmp(cmd_str,fg_str,MAXLINE)==0)
    {
      int prevState = jobs[jobindex].state;
      //FG
      //Set this job as in the forground
      jobs[jobindex].state = FG;
      if(prevState == ST)
      {
	//Send cont signal
	//printf("Sending cont\n");
	kill(jobs[jobindex].pid,SIGCONT);
	if(jobs[jobindex].pid2 !=0)
	{
	  kill(jobs[jobindex].pid2,SIGCONT);
	}
      }
      //Wait on this process
      waitfg(jobs[jobindex].pid);
      //if second process exists wait on that too
      if(jobs[jobindex].pid2 !=0)
      {
	waitfg(jobs[jobindex].pid2);
      }
    }
    else
    {
      app_error("Error in bgfg execution - cmd not recognized\n");
    }
    
  
    return;
}

/* 
 * waitfg - Block until process pid is no longer the foreground process
 */
void waitfg(pid_t pid)
{
    //Find the job in the jobs list
    int i;
    int jobindex = -1;

    if (pid < 1)
	app_error("waitfg: cannot wait on pid");

    for (i = 0; i < MAXJOBS; i++) 
    {
	if ((jobs[i].pid == pid) || (jobs[i].pid2 == pid) )
	{
	  jobindex = i;
	  break;
	}
    }
    if(jobindex <0)
    {
      printf("Could not find job in job list");
      return;
    }
     
    //Wait without hanging for the pid passed
    //Use sleep to avoid 'busy' wait
    //Wait while process is alive (no hang wait) and in fg
    //Waitpid returns 0 if nothing to wait for (process is not dead)     //Check that job is in FG
    int status;
    while(    ((waitpid(pid, &status, WNOHANG))<=0)          &&          (jobs[jobindex].state==FG)     )
    {
      //printf("waiting for child %d with state %d and waitpid returning %d \n",pid,jobs[jobindex].state,waitpid(pid, &status, WNOHANG));
      sleep(1);
    }
    //printf("waitfg finished child %d with state %d and waitpid returning %d \n",pid,jobs[jobindex].state,waitpid(pid, &status, WNOHANG));
  
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
  //You will want to check for each of these conditions, 
  //in case the process has been stopped or 
  //terminated abruptly due to an unhandled signal, 
  //when you handle sigchld.
  
  pid_t pid;
  int status;

  //Wait without hanging to reap all children.
  while((pid = waitpid(-1, &status, WNOHANG)) > 0)
  {
    //Get the job that goes with this pid
    //Find the job in the jobs list
    int i;
    int jobindex = -1;

    for (i = 0; i < MAXJOBS; i++) 
    {
	if ((jobs[i].pid == pid) || (jobs[i].pid2 == pid) )
	{
	  jobindex = i;
	  break;
	}
    }
    
    //Second process exists
    if(jobs[jobindex].pid2!=0)
    {
      //If this pid is the second one
      if(pid==jobs[jobindex].pid2)
      {
	//Kill the first one
	kill(jobs[jobindex].pid,SIGTERM);
      }
      //If this pid is the first one
      else
      {
	//Kill the second one
	kill(jobs[jobindex].pid2,SIGTERM);
      }
    }

    
    if(WIFEXITED(status))
    {
      //printf("Child with pid %d exited normally.\n", pid);
      //Remove child from list

      deletejob(jobs,pid);
    }
    else if(WIFSIGNALED(status))
    {
      //printf("Child with pid %d exited because of uncaught signal number %d.\n", pid, WTERMSIG(status));
      //Remove child from list
      deletejob(jobs,pid);
    }
    else if(WIFSTOPPED(status))
    {
      //Do something here?
      //printf("Child with pid %d is stopped.\n", pid);     
    }
    else
    {
      //printf("Child with pid %d was reaped for an unknown reason.\n", pid);
      //Remove child from list
      deletejob(jobs,pid);
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
    //Find foregeround job
    int i;
    int jobindex = -1;

    for (i = 0; i < MAXJOBS; i++) 
    {
	if (jobs[i].state == FG) 
	{
	  jobindex = i;
	  break;
	}
    }
    if(jobindex <0)
    {
      //Could not find job in list that is foreground
      //Exit the program
      printf("Exiting Shell.\n");      
      exit(0);
    }
    
    //Get pid from jobs list entry
    int foregroundpid = jobs[jobindex].pid;
  
    //Send to forground job
    printf("Job [%d] (%d) terminated by signal %d\n", jobs[jobindex].jid, jobs[jobindex].pid,sig);
    kill(foregroundpid,SIGINT);  

    return;
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.  
 */
void sigtstp_handler(int sig) 
{
    //Find foregeround job
    int i;
    int jobindex = -1;

    for (i = 0; i < MAXJOBS; i++) 
    {
	if (jobs[i].state == FG) 
	{
	  jobindex = i;
	  break;
	}
    }
    if(jobindex <0)
    {
      //Do nothing
      return;
    }
    
    //Get pid from jobs list entry
    int foregroundpid = jobs[jobindex].pid;
  
    //Send to forground job
    //Record new state
    jobs[jobindex].state = ST;
    printf("Job [%d] (%d) stopped by signal %d\n", jobs[jobindex].jid, jobs[jobindex].pid,sig);
    kill(foregroundpid,SIGTSTP);
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
    job->pid2 = 0;
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
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline, pid_t pid2) 
{
    int i;
    
    if (pid < 1)
	return 0;

    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid == 0) {
	    jobs[i].pid = pid;
	    jobs[i].pid2 = pid2;
	    jobs[i].state = state;
	    jobs[i].jid = nextjid++;
	    if (nextjid > MAXJOBS)
		nextjid = 1;
	    strcpy(jobs[i].cmdline, cmdline);
	    if(state == BG)
	    {
	      //Print bg info
	      printf("[%d] (%d) %s", jobs[i].jid, jobs[i].pid,jobs[i].cmdline);
	    }
	    
	    
  	    if(verbose){
	        printf("Added job [%d] %d %s pid2:%d\n", jobs[i].jid, jobs[i].pid, jobs[i].cmdline,jobs[i].pid2);
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
	if ( (jobs[i].pid == pid) || (jobs[i].pid2 == pid) ) {
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
	if ( (jobs[i].pid == pid) || (jobs[i].pid2 == pid) )
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
	if ( (jobs[i].pid == pid) || (jobs[i].pid2 == pid) ) {
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