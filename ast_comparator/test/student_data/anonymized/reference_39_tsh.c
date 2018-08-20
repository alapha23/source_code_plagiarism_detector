/ * 
 * Tsh - A tiny shell program with job control 
 * 
 * Clara Raubertas Ltd. 
 * Clara1 
 * / 
 # Include <stdio.h> 
 # Include <stdlib.h> 
 # Include <unistd.h> 
 # Include <string.h> 
 # Include <ctype.h> 
 # Include <signal.h> 
 # Include <sys/types.h> 
 # Include <sys/wait.h> 
 # Include <errno.h> 

/ * Misc manifest constants * / 
 # Define MAXLINE 1024 / * max line size * / 
 # Define MAXARGS 128 / * max args on a command line * / 
 # Define MAXJOBS 16 / * max jobs at any point in time * / 
 # Define MAXJID 1 << 16 / * max job ID * / 

/ * Job states * / 
 # Define UNDEF 0 / * undefined * / 
 # Define FG 1 / * running in foreground * / 
 # Define BG 2 / * running in background * / 
 # Define ST 3 / * stopped * / 

/ * 
 * Jobs states: FG (foreground), BG (background), ST (stopped) 
 * Job state transitions and enabling actions: 
 * FG -> ST: ctrl-z 
 * ST -> FG: fg command 
 * ST -> BG: bg command 
 * BG -> FG: fg command 
 * At most 1 job can be in the FG state. 
 * / 

/ * Global variables * / 
extern char ** environ; / * defined in libc * / 
 char prompt [] = "tsh>"; / * command line prompt (DO NOT CHANGE) * / 
 int verbose = 0; / * if true, print additional output * / 
 int nextjid = 1; / * next job ID to allocate * / 
 char sbuf [MAXLINE]; / * for composing sprintf messages * / 

struct job_t {/ * The job struct * / 
 pid_t pid; / * job PID * / 
 int JID; / * job ID [1, 2, ...] * / 
 int state; / * UNDEF, BG, FG, or ST * / 
 char cmdline [MAXLINE]; / * command line * / 
 }; 
 struct job_t jobs [MAXJOBS]; / * The job list * / 
 / * End global variables * / 


/ * Function prototypes * / 

/ * Here are the functions that you will implement * / 
 void eval (char * cmdline); 
 to int builtin_cmd (char ** argv); 
 void do_bgfg (char ** argv); 
 void waitfg (pid_t pid); 

void sigchld_handler (int sig); 
 void sigtstp_handler (int sig); 
 void sigint_handler (int sig); 

/ * Here are helper routines that we've provided for you * / 
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
 to int pid2jid (pid_t pid); 
 void listjobs (struct job_t * jobs); 

void usage (void); 
 void unix_error (char * msg); 
 void app_error (char * msg); 
 the typedef void handler_t (int); 
 handler_t * Signal (int signum, handler_t * handler); 

/ * 
 * Main - The shell's main routine 
 * / 
 int main (int argc, char ** argv) 
 { 
 char c; 
 char cmdline [MAXLINE]; 
 int emit_prompt = 1; / * emit prompt (default) * / 

/ * Redirect stderr to stdout (so that driver will get all output 
 * On the pipe connected to stdout) * / 
 dup2 (1, 2); 

/ * Parse the command line * / 
 while ((c = getopt (argc, argv, "hvp"))! = EOF) { 
 switch (c) { 
 case 'h': / * print help message * / 
 usage (); 
 break; 
 case 'v': / * emit additional diagnostic info * / 
 verbose = 1; 
 break; 
 case 'p': / * don't print a prompt * / 
 emit_prompt = 0; / * handy for automatic testing * / 
 break; 
 default: 
 usage (); 
 } 
 } 

/ * Install the signal handlers * / 

/ * These are the ones you will need to implement * / 
 Signal (SIGINT, sigint_handler); / * ctrl-c * / 
 Signal (SIGTSTP, sigtstp_handler); / * ctrl-z * / 
 Signal (SIGCHLD, sigchld_handler); / * Terminated or stopped child * / 

/ * This one provides a clean way to kill the shell * / 
 Signal (SIGQUIT sigquit_handler,); 

/ * Initialize the job list * / 
 initjobs (jobs); 

/ * Execute the shell's read / eval loop * / 
 while (1) { 

/ * Read command line * / 
 if (emit_prompt) { 
 printf ("% s", prompt); 
 fflush (stdout); 
 } 
 if ((fgets (cmdline, MAXLINE, stdin) == NULL) && ferror (stdin)) 
 app_error ("fgets error"); 
 if (feof (stdin)) {/ * End of file (ctrl-d) * / 
 fflush (stdout); 
 exit (0); 
 } 

/ * Evaluate the command line * / 
 eval (cmdline); 
 fflush (stdout); 
 fflush (stdout); 
 } 

exit (0); / * control never reaches here * / 
 } 

/ * 
 * Eval - Evaluate the command line that the user has just typed in 
 * 
 * If the user has requested a built-in command (quit, jobs, bg or fg) 
 * Then execute it immediately. Otherwise, fork a child process and 
 * Run the job in the context of the child. If the job is running in 
 * The foreground, wait for it to terminate and then return. Note: 
 * Each child process must have a unique process group ID so that our 
 * Background children don't receive SIGINT (SIGTSTP) from the kernel 
 * When we type ctrl-c (ctrl-z) at the keyboard. 
 * / 
 void eval (char * cmdline) 
 { 
 char * argv [MAXLINE]; 
 pid_t pid; 
 sigset_t mask; 
 int bg = parseline (cmdline, argv); 
 / / Parses the command line and indicates whether the command is to run in the background 
 if (argv [0] == NULL) { 
 return; / / ignore blank lines 
 } 
 if ((sigemptyset to (& mask)) <0) { 
 unix_error ("sigemptyset error"); 
 } 
 if ((sigaddset to (& mask, SIGCHLD)) <0) { 
 unix_error ("sigaddset error"); 
 } 
 if (! (builtin_cmd (argv))) {/ / first check if it is a builtin command 
 if ((sigprocmask (SIG_BLOCK, & mask, NULL)) <0) { 
 unix_error ("sigprocmask error"); 
 } 
 / / Fork off and execute the program in the child 
 if ((pid = fork ()) <0) { 
 unix_error ("fork error"); 
 } 
 else if (PID == 0) { 
 if (setpgid (0, 0) <0) { 
 / / Make a new process group, check for error 
 unix_error ("setpgid error"); 
 } 
 if (sigprocmask (SIG_UNBLOCK, & mask, NULL) <0) { 
 unix_error ("sigprogmask error"); 
 } 
 / / Printf ("child process group is% d / n", getpgid (0)); 
 if (execve (argv [0], argv, environ) <0) { 
 / / Error check execve 
 printf ("% s: Command not found. / n", argv [0]); 
 / / Return; 
 exit (0); 
 } 
 } 
 / / Printf ("parent process group is% d / n", getpgid (0)); 
 if (bg) { 
 / / Run the process in the foreground 
 addjob (jobs, pid, FG, cmdline); 
 if (sigprocmask (SIG_UNBLOCK, & mask, NULL) <0) { 
 unix_error ("sigprocmask error"); 
 } 
 waitfg (pid); / / wait for the foreground child to terminate. 
 } 
 else { 
 / / Run the process in the background 
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

/ * 
 * Parseline - Parse the command line and build the argv array. 
 * 
 * Characters enclosed in single quotes are treated as a single 
 * Argument. Return true if the user has requested a BG job, false if 
 * The user has requested a FG job. 
 * / 
 int parseline (const char * cmdline, char ** argv) 
 { 
 static char array [MAXLINE]; / * holds local copy of command line * / 
 char * buf = array; / * ptr that traverses command line * / 
 char * delim; / * points to first space delimiter * / 
 int argc; / * number of args * / 
 int bg; / * background job? * / 

strcpy (buf, cmdline); 

buf [strlen (buf) -1] = ''; / * replace trailing '/ n' with space * / 
 while (* buf && (* buf == '')) / * ignore leading spaces * / 
 buf + +; 

/ * Build the argv list * / 
 argc = 0; 
 if (* buf == '/'') { 
 buf + +; 
 delim = strchr (buf, '/''); 
 } 
 else { 
 delim = strchr (buf, ''); 
 } 
 while (delim) { 
 argv [argc + +] = buf; 
 * Delim = '/ 0'; 
 buf = delim + 1; 
 while (* buf && (* buf == '')) / * ignore spaces * / 
 buf + +; 
 if (* buf == '/'') { 
 buf + +; 
 delim = strchr (buf, '/''); 
 } 
 else { 
 delim = strchr (buf, ''); 
 } 
 } 
 argv [argc] = NULL; 

if (argc == 0) / * ignore blank line * / 
 return 1; 
 / * Should the job run in the background? * / 
 if ((bg = (* argv [argc-1] == '&'))! = 0) { 
 argv [- argc] = NULL; 
 } 
 return bg; 
 } 

/ * 
 * Builtin_cmd - If the user has typed a built-in command then execute 
 * It immediately. 
 * / 

the int builtin_cmd (char ** argv) 

{ 
 / / Printf ("builtin_cmd? / N"); 
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
 return 0; / * not a builtin command * / 
 } 

/ * 
 * Do_bgfg - Execute the builtin bg and fg commands 
 * / 
 the void do_bgfg (char ** argv) 
 { 
 if ((strcmp ("bg", argv [0]))) { 
 / / Make the job bg 
 if (! (strncpy (argv [1], "%"))) { 
 getjobjid (jobs, atoi (argv [1])); 
 } 
 else { 
 kill (atoi (argv [1]), SIGCONT); 
 (* (Getjobpid (jobs, atoi (argv [1])))). State = BG; 
 } 
 / / Find out if it is a pid or jid 
 / / Send the process a sigcont 
 / / Set its state to bg 
 } 
 if ((strcmp ("FG", argv [0]))) { 
 / / Make the job fg 
 / / Find out if it is a pid or jid 
 / / Send it a sigcont 
 / / Set its state to fg 
 / / Waitfg 
 } 
 printf ("% s / n", argv [0]); 
 return; 
 } 

/ * 
 * Waitfg - Block until process pid is no longer the foreground process 
 * / 
 void waitfg (pid_t pid) 
 { 
 while 
 (Getjobpid (jobs, pid)! = NULL) { 
 if ((* (getjobpid (jobs, pid))). state == FG) { 
 / / Printf ("% d / n", pid); 
 sleep (1); 
 } 
 } 
 return; 
 } 

/ ***************** 
 * Signal handlers 
 ***************** / 

/ * 
 * Sigchld_handler - The kernel sends a SIGCHLD to the shell whenever 
 * A child job terminates (becomes a zombie), or stops because it 
 * Received a SIGSTOP or SIGTSTP signal. The handler reaps all 
 * Available zombie children, but doesn't wait for any other 
 * Currently running children to terminate. 
 * / 
 the void sigchld_handler (int sig) 
 { 
 int status; 
 pid_t pid; 
 / / Pid = fgpid (jobs); 
 while ((pid = waitpid (fgpid (jobs), & status, WNOHANG | WUNTRACED))> 0) { 
 / / Printf ("waitpid:% d / n", pid); 
 deletejob (jobs, pid); 
 } 
 the If (errno = ECHILD) { 
 unix_error ("sigchld_handler: waitpid error"); 
 } 
 return; 
 } 

/ * Sigint_handler - The kernel sends a SIGINT to the shell whenver the 
 * User types ctrl-c at the keyboard. Catch it and send it along 
 * To the foreground job. 
 * / 
 the void sigint_handler (int sig) 
 { 
 int pid; 
 int jid; 
 the pid = fgpid (jobs); 
 if (pid = 0) { 
 jid = pid2jid (pid); 
 kill (-pid, SIGKILL); 
 printf ("Job [% d] (% d) terminated by signal% d / n", jid, pid, sig); 
 } 
 else { 
 } 
 return; 
 } 

/ * 
 * Sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever 
 * The user types ctrl-z at the keyboard. Catch it and suspend the 
 * Foreground job by sending it a SIGTSTP. 
 * / 
 the void sigtstp_handler (int sig) 
 { 
 int pid; 
 int jid; 
 int GID; 
 the pid = fgpid (jobs); 

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
 / * Test05, test07, test08, test09, test10, test12, test13, test14, test15, test16 * / 
 / ********************* 
 * End signal handlers 
 ********************* / 

/ *********************************************** 
 * Helper routines that manipulate the job list 
 ********************************************** / 

/ * Clearjob - Clear the entries in a job struct * / 
 void clearjob (struct job_t * job) { 
 job-> pid = 0; 
 job-> jid = 0; 
 job-> state = UNDEF; 
 job-> cmdline [0] = '/ 0'; 
 } 

/ * Initjobs - Initialize the job list * / 
 void initjobs (struct job_t * jobs) { 
 int i; 

for (i = 0; i <MAXJOBS; i + +) 
 clearjob (& jobs [i]); 
 } 

/ * Maxjid - Returns largest allocated job ID * / 
 int maxjid (struct job_t * jobs) 
 { 
 int i, max = 0; 
 for (i = 0; i <MAXJOBS; i + +) 
 if (jobs [i]. jid> max) 
 max = jobs [i] jid; 
 return max; 
 } 

/ * Addjob - Add a job to the job list * / 
 int addjob (struct job_t * jobs, pid_t pid, int state, char * cmdline) { 
 int i; 

if (pid <1) 
 return 0; 

for (i = 0; i <MAXJOBS; i + +) { 
 if (jobs [i] PID == 0) { 
 jobs [i] pid = pid; 
 jobs [i] state = state; 
 jobs [i]. jid = nextjid + +; 
 the if (nextjid> maxjobs) 
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

/ * Deletejob - Delete a job whose PID = pid from the job list * / 
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

/ * Fgpid - Return PID of current foreground job, 0 if no such job * / 
 pid_t fgpid (struct job_t * jobs) { 
 int i; 

for (i = 0; i <MAXJOBS; i + +) 
 if (jobs [i] state == FG) 
 the return jobs [i] pid; 
 return 0; 
 } 

/ * Getjobpid - Find a job (by PID) on the job list * / 
 struct job_t * getjobpid (struct job_t * jobs, pid_t pid) { 
 int i; 

if (pid <1) 
 return NULL; 
 for (i = 0; i <MAXJOBS; i + +) 
 if (jobs [i] pid == pid) 
 Return & jobs [i]; 
 return NULL; 
 } 

/ * Getjobjid - Find a job (by JID) on the job list * / 
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

/ * Pid2jid - Map process ID to job ID * / 
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

/ * Listjobs - Print the job list * / 
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
 / ************************************************************ 
 * End job list helper routines 
 ************************************************************ / 


/ *********************** 
 * Other helper routines 
 *********************** / 

/ * 
 * Usage - print a help message 
 * / 
 void usage (void) 
 { 
 printf ("Usage: shell [-hvp] / n"); 
 printf ("-h print this message / n"); 
 printf ("-v print additional diagnostic information / n"); 
 printf ("-p do not emit a command prompt / n"); 
 Exit (1); 
 } 

/ * 
 * Unix_error - unix-style error routine 
 * / 
 the void unix_error (char * msg) 
 { 
 fprintf (stdout, "% s:% s / n", msg, strerror (errno)); 
 Exit (1); 
 } 

/ * 
 * App_error - application-style error routine 
 * / 
 the void app_error (char * msg) 
 { 
 fprintf (stdout, "% s / n", msg); 
 Exit (1); 
 } 

/ * 
 * Signal - wrapper for the sigaction function 
 * / 
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

/ * 
 * Sigquit_handler - The driver program can gracefully terminate the 
 * Child shell by sending it a SIGQUIT signal. 
 * / 
 the void sigquit_handler (int sig) 
 { 
 printf ("Terminating after receipt of SIGQUIT signal / n"); 
 Exit (1); 
 } 

