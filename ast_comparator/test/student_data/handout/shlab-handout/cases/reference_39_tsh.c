/*** 
 * Tsh - A tiny shell program with job control 
 * 
 * Clara Raubertas Ltd. 
 * Clara1 
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

/** Misc manifest constants */
 #define MAXLINE 1024 /** max line size */ 
 #define MAXARGS 128 /** max args on a command line */ 
 #define MAXJOBS 16 /** max jobs at any point in time */ 
 #define MAXJID 1 << 16 /** max job ID */ 

/** Job states */ 
 #define UNDEF 0 /** undefined */ 
 #define FG 1 /** running in foreground */ 
 #define BG 2 /** running in background */ 
 #define ST 3 /** stopped */ 

/** 
 * Jobs states: FG (foreground), BG (background), ST (stopped) 
 * Job state transitions and enabling actions: 
 * FG -> ST: ctrl-z 
 * ST -> FG: fg command 
 * ST -> BG: bg command 
 * BG -> FG: fg command 
 * At most 1 job can be in the FG state. 
 */ 

/** Global variables */ 
extern char ** environ; /** defined in libc */ 
 char prompt [] = "tsh>"; /** command line prompt (DO NOT CHANGE) */ 
 int verbose = 0; /** if true, print additional output */ 
 int nextjid = 1; /** next job ID to allocate */ 
 char sbuf [MAXLINE]; /** for composing sprintf messages */ 

struct job_t {/** The job struct */ 
 pid_t pid; /** job PID */ 
 int JID; /** job ID [1, 2, ...] */ 
 int state; /** UNDEF, BG, FG, or ST */ 
 char cmdline [MAXLINE]; /** command line */ 
 }; 
 struct job_t jobs [MAXJOBS]; /** The job list */ 
 /** End global variables */ 


/** Function prototypes */ 

/** Here are the functions that you will implement */ 
 void eval (char * cmdline); 
 int builtin_cmd (char ** argv); 
 void do_bgfg (char ** argv); 
 void waitfg (pid_t pid); 

void sigchld_handler (int sig); 
 void sigtstp_handler (int sig); 
 void sigint_handler (int sig); 

/** Here are helper routines that we've provided for you */ 
 int parseline (const char * cmdline, char ** argv); 
 void sigquit_handler (int sig); 

void clearjob (struct job_t * job); 
 void initjobs (struct job_t * jobs); 
 int maxjid (struct job_t * jobs); 
 int addjob (struct job_t * jobs, pid_t pid, int state, char * cmdline); 
 int deletejob (struct job_t * jobs, pid_t pid); 
 pid_t fgpid (struct job_t * jobs); 
 struct job_t * getjobpid (struct job_t * jobs, pid_t pid); 
 struct job_t * getjobjid (struct job_t * jobs, int jid); 
 int pid2jid (pid_t pid); 
 void listjobs (struct job_t * jobs); 

void usage (void); 
 void unix_error (char * msg); 
 void app_error (char * msg); 
 typedef void handler_t (int); 
 handler_t *Signal(int signum, handler_t *handler); 

/** 
 * Main - The shell's main routine 
 */ 
 int main (int argc, char ** argv) 
 { 
 char c; 
 char cmdline [MAXLINE]; 
 int emit_prompt = 1; /** emit prompt (default) */ 

/** Redirect stderr to stdout (so that driver will get all output 
 * On the pipe connected to stdout) */ 
 dup2 (1, 2); 

/** Parse the command line */ 
 while ((c = getopt (argc, argv, "hvp"))!= EOF) { 
 switch (c) { 
 case 'h': /** print help message */ 
 usage (); 
 break; 
 case 'v': /** emit additional diagnostic info */ 
 verbose = 1; 
 break; 
 case 'p': /** don't print a prompt */ 
 emit_prompt = 0; /** handy for automatic testing */ 
 break; 
 default: 
 usage (); 
 } 
 } 

/** Install the signal handlers */ 

/** These are the ones you will need to implement */ 
 Signal (SIGINT, sigint_handler); /** ctrl-c */ 
 Signal (SIGTSTP, sigtstp_handler); /** ctrl-z */ 
 Signal (SIGCHLD, sigchld_handler); /** Terminated or stopped child */ 

 Signal (SIGQUIT sigquit_handler); 

 initjobs (jobs); 

 while (1) { 

/** Read command line */ 
 if (emit_prompt) { 
 printf ("% s", prompt); 
 fflush (stdout); 
 } 
 if ((fgets (cmdline, MAXLINE, stdin) == NULL) && ferror (stdin)) 
 app_error ("fgets error"); 
 if (feof (stdin)) {/** End of file (ctrl-d) */ 
 fflush (stdout); 
 exit (0); 
 } 

 eval (cmdline); 
 fflush (stdout); 
 fflush (stdout); 
 } 

exit (0); /** control never reaches here **/ 
 } 

/* 
 * Eval - Evaluate the command line that the user has just typed in 
 * 
 * If the user has requested a built-in command (quit, jobs, bg or fg) 
 * Then execute it immediately. Otherwise, fork a child process and 
 * Run the job in the context of the child. If the job is running in 
 * The foreground, wait for it o terminate and then return. Note: 
 * Each child process must have a unique process group ID so that our 
 * Background children don't receive SIGINT (SIGTSTP) from the kernel 
 * When we type ctrl-c (ctrl-z) at the keyboard. 
 */ 
 void eval (char * cmdline) 
 { 
 char * argv [MAXLINE]; 
 pid_t pid; 
 sigset_t mask; 
 int bg = parseline (cmdline, argv); 
 if (argv [0] == NULL) { 
 return; //ignore blank lines 
 } 
 if ((sigemptyset (& mask)) <0) { 
 unix_error ("sigemptyset error"); 
 } 
 if ((sigaddset(& mask, SIGCHLD)) <0) { 
 unix_error ("sigaddset error"); 
 } 
 if (! (builtin_cmd (argv))) {// first check if it is a builtin command 
 if ((sigprocmask (SIG_BLOCK, & mask, NULL)) <0) { 
 unix_error ("sigprocmask error"); 
 } 
 if ((pid = fork ()) <0) { 
 unix_error ("fork error"); 
 } 
 else if (PID == 0) { 
 if (setpgid (0, 0) <0) { 
 // Make a new process group, check for error 
 unix_error ("setpgid error"); 
 } 
 if (sigprocmask (SIG_UNBLOCK, & mask, NULL) <0) { 
 unix_error ("sigprogmask error"); 
 } 
 // Printf ("child process group is% d / n", getpgid (0)); 
 if (execve (argv [0], argv, environ) <0) { 
 // Error check execve 
 printf ("% s: Command not found. / n", argv [0]); 
 // Return; 
 exit (0); 
 } 
 } 
 // Printf ("parent process group is% d / n", getpgid (0)); 
 if (bg) { 
 addjob (jobs, pid, FG, cmdline); 
 if (sigprocmask (SIG_UNBLOCK, & mask, NULL) <0) { 
 unix_error ("sigprocmask error"); 
 } 
 waitfg (pid); // wait for the foreground child to terminate. 
 } 
 else { 
 int jid; 
 addjob (jobs, pid, BG, cmdline); 
 if (sigprocmask (SIG_UNBLOCK, & mask, NULL) <0) { 
 unix_error ("sigprocmask error"); 
 } 
 jid = pid2jid (pid); 
 printf ("[% d] (% d)% s", jid, pid, cmdline); 
 } 
 } 
 return; 
 } 

/* 
 * 
 * Characters enclosed in single quotes are treated as a single 
 * The user has requested a FG job. 
 */ 
 int parseline (const char * cmdline, char ** argv) 
 { 
 	static char array [MAXLINE]; /** holds local copy of command line */
	char * buf = array; /** ptr that traverses command line */ 
	char * delim; /** points to first space delimiter */ 
	int argc; /** number of args */ 
	int bg; /** background job? */ 

	strcpy(buf, cmdline);

	buf[strlen(buf) -1] = ' '; /** replace trailing '/ n' with space */ 
	while (*buf && (*buf == ' ')) /** ignore leading spaces */ 
		buf++; 
	
 argc = 0; 
 if (* buf == '/'){ 
 buf++; 
 delim = strchr (buf, '/'); 
 } 
 else { 
 delim = strchr (buf, ' '); 
 } 
 while (delim) { 
 argv [argc + +] = buf; 
 * Delim = '/0'; 
 buf = delim + 1; 
 while (* buf && (* buf == ' ')) /* ignore spaces */ 
 buf++; 
 if (* buf == '/') { 
 buf++; 
 delim = strchr (buf, '/'); 
 } 
 else { 
 delim = strchr (buf, ' '); 
 } 
 } 
 argv [argc] = NULL; 

if (argc == 0) /** ignore blank line */ 
 return 1; 
 if ((bg = (* argv [argc-1] == '&'))! = 0) { 
 argv [- argc] = NULL; 
 } 
 return bg; 
 } 


int builtin_cmd (char ** argv) 

{ 
 // Printf ("builtin_cmd? / N"); 
 if (! strcmp ("quit", argv [0])) { 
 exit (0); 
 } 
 if (! strcmp ("jobs", argv [0])) { 
 listjobs (jobs); 
 return 1; 
 } 
 if (! strcmp ("bg", argv [0]) | |! (strcmp ("FG", argv [0]))) { 
 do_bgfg (argv); 
 return 1; 
 } 
 return 0; /* not a builtin command */ 
 } 

 void do_bgfg (char ** argv) 
 { 
 if ((strcmp ("bg", argv [0]))) { 
 if (! (strncpy (argv [1], "%"))) { 
 getjobjid (jobs, atoi (argv [1])); 
 } 
 else { 
 kill (atoi (argv [1]), SIGCONT); 
 (* (Getjobpid (jobs, atoi (argv [1])))). State = BG; 
 } 
 } 
 if ((strcmp ("FG", argv [0]))) { 
 } 
 printf ("% s / n", argv [0]); 
 return; 
 } 

 void waitfg (pid_t pid) 
 { 
 while 
 (Getjobpid (jobs, pid)! = NULL) { 
 if ((* (getjobpid (jobs, pid))). state == FG) { 
 // Printf ("% d / n", pid); 
 sleep (1); 
 } 
 } 
 return; 
 } 

 void sigchld_handler (int sig) 
 { 
 int status; 
 pid_t pid; 
 // Pid = fgpid (jobs); 
 while ((pid = waitpid (fgpid (jobs), & status, WNOHANG | WUNTRACED))> 0) { 
 // Printf ("waitpid:% d / n", pid); 
 deletejob (jobs, pid); 
 } 
 if (errno = ECHILD) { 
 unix_error ("sigchld_handler: waitpid error"); 
 } 
 return; 
 } 

 void sigint_handler (int sig) 
 { 
 int pid; 
 int jid; 
 pid = fgpid (jobs); 
 if (pid = 0) { 
 jid = pid2jid (pid); 
 kill (-pid, SIGKILL); 
 printf ("Job [% d] (% d) terminated by signal% d / n", jid, pid, sig); 
 } 
 else { 
 } 
void sigtstp_handler (int sig) 
 { 
 int pid; 
 int jid; 
 int GID; 
 pid = fgpid (jobs); 

if (pid = 0) { 
 jid = pid2jid (pid); 
 gid = -1 * pid; 
 kill (-pid, SIGTSTP); 
 (* (Getjobpid (jobs, pid))). State = ST; 
 printf ("Stopped [% d] (% d)% s", jid, pid, (* (getjobpid (jobs, pid))). cmdline); 
 } 
 else { 
 } 
 return; 
 } 

 void clearjob (struct job_t * job) { 
 job-> pid = 0; 
 job-> jid = 0; 
 job-> state = UNDEF; 
 job-> cmdline [0] = '/0'; 
 } 

 void initjobs (struct job_t * jobs) { 
 int i; 

for (i = 0; i <MAXJOBS; i + +) 
 clearjob (& jobs [i]); 
 } 

 int maxjid (struct job_t * jobs) 
 { 
 int i, max = 0; 
 for (i = 0; i <MAXJOBS; i + +) 
 if (jobs [i]. jid> max) 
 max = jobs [i] jid; 
 return max; 
 } 

 int addjob (struct job_t * jobs, pid_t pid, int state, char * cmdline) { 
 int i; 

if (pid <1) 
 return 0; 

for (i = 0; i <MAXJOBS; i + +) { 
 if (jobs [i] PID == 0) { 
 jobs [i] pid = pid; 
 jobs [i] state = state; 
 jobs [i]. jid = nextjid + +; 
 if (nextjid> maxjobs) 
 nextjid = 1; 
 strcpy (jobs [i]. cmdline, cmdline); 
 if (verbose) { 
 printf ("Added job [% d]% d% s:% s / n", jobs [i]. jid, jobs [i]. pid, jobs [i]. cmdline, cmdline); 
 } 
 return 1; 
 } 
 } 
 printf ("Tried to create too many jobs / n"); 
 return 0; 
 } 

 int deletejob (struct job_t * jobs, pid_t pid) 
 { 
 int i; 

if (pid <1) 
 return 0; 

for (i = 0; i <MAXJOBS; i + +) { 
 if (jobs [i] pid == pid) { 
 clearjob (& jobs [i]); 
 nextjid = maxjid (jobs) +1; 
 return 1; 
 } 
 } 
 return 0; 
 } 

 pid_t fgpid (struct job_t * jobs) { 
 int i; 

for (i = 0; i <MAXJOBS; i + +) 
 if (jobs [i] state == FG) 
 return jobs [i] pid; 
 return 0; 
 } 

 struct job_t * getjobpid (struct job_t * jobs, pid_t pid) { 
 int i; 

if (pid <1) 
 return NULL; 
 for (i = 0; i <MAXJOBS; i + +) 
 if (jobs [i] pid == pid) 
 Return & jobs [i]; 
 return NULL; 
 } 

 struct job_t * getjobjid (struct job_t * jobs, int jid) 
 { 
 int i; 

if (jid <1) 
 return NULL; 
 for (i = 0; i <MAXJOBS; i + +) 
 if (jobs [i]. jid == jid) 
 Return & jobs [i]; 
 return NULL; 
 } 

 int pid2jid (pid_t pid) 
 { 
 int i; 

if (pid <1) 
 return 0; 
 for (i = 0; i <MAXJOBS; i + +) 
 if (jobs [i] pid == pid) { 
 return jobs [i]. jid; 
 } 
 return 0; 
 } 

 void listjobs (struct job_t * jobs) 
 { 
 int i; 

for (i = 0; i <MAXJOBS; i + +) { 
 if (jobs [i] pid! = 0) { 
 printf ("[% d] (% d)", jobs [i]. jid jobs [i] pid); 
 switch (jobs [i] state) { 
 CASE BG: 
 printf ("Running"); 
 break; 
 case FG: 
 printf ("Foreground"); 
 break; 
 case ST: 
 printf ("Stopped"); 
 break; 
 default: 
 printf ("listjobs: Internal error: job [% d]. state =% d", 
 i, jobs [i] state); 
 } 
 printf ("% s", jobs [i]. cmdline); 
 } 
 } 
 } 


 void usage (void) 
 { 
 printf ("Usage: shell [-hvp] / n"); 
 printf ("-h print this message / n"); 
 printf ("-v print additional diagnostic information / n"); 
 printf ("-p do not emit a command prompt / n"); 
 Exit (1); 
 } 

 void unix_error (char * msg) 
 { 
 fprintf (stdout, "% s:% s / n", msg, strerror (errno)); 
 Exit (1); 
 } 

 void app_error (char * msg) 
 { 
 fprintf (stdout, "% s / n", msg); 
 Exit (1); 
 } 

 handler_t * Signal (int signum, handler_t * handler) 
 { 
 struct sigaction action, old_action; 

action.sa_handler = handler; 
 sigemptyset (& action.sa_mask); / * block sigs of type being handled * / 
 action.sa_flags = SA_RESTART; / * restart syscalls if possible * / 

if (sigaction (signum, & action, & old_action) <0) 
 unix_error ("Signal error"); 
 return (old_action.sa_handler); 
 } 

 void sigquit_handler (int sig) 
 { 
 printf ("Terminating after receipt of SIGQUIT signal / n"); 
 Exit (1); 
 } 

