/* 
* tsh - A tiny shell program with job control
* rommel @copyright
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
extern char **environ; /* defined in libc */
char prompt[] = &quot;tsh> &quot;; /* command line prompt (DO NOT CHANGE) */
int verbose = 0; /* if true, print additional output */
int nextjid = 1; /* next job ID to allocate */
char sbuf[MAXLINE]; /* for composing sprintf messages */
struct job_t { /* The job struct */
pid_t pid; /* job PID */
int jid; /* job ID [1, 2, ...] */
int state; /* UNDEF, BG, FG, or ST */
char cmdline[MAXLINE]; /* command line */
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
sigset_t mask_signal(int how, int sig);
int send_signal(pid_t pid, int sig);
/* Here are helper routines that we've provided for you */
void do_echo(char **argv);
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
int main(int argc, char **argv) {
char c;
char cmdline[MAXLINE];
int emit_prompt = 1; /* emit prompt (default) */
/* Redirect stderr to stdout (so that driver will get all output
* on the pipe connected to stdout) */
dup2(1, 2);
/* Parse the command line */
while ((c = getopt(argc, argv, &quot;hvp&quot;)) != EOF) {
switch (c) {
case 'h': /* print help message */
usage();
break;
case 'v': /* emit additional diagnostic info */
verbose = 1;
break;
case 'p': /* don't print a prompt */
emit_prompt = 0; /* handy for automatic testing */
break;
default:
usage();
}
}
/* Install the signal handlers */
/* 信号处理函数 */
Signal(SIGINT, sigint_handler); /* ctrl-c */
Signal(SIGTSTP, sigtstp_handler); /* ctrl-z */
Signal(SIGCHLD, sigchld_handler); /* Terminated or stopped child */
/* This one provides a clean way to kill the shell */
Signal(SIGQUIT, sigquit_handler);
/* Initialize the job list */
initjobs(jobs);
/* Shell输入程序 */
while (1) {
/* Read command line */
if (emit_prompt) {
printf(&quot;%s&quot;, prompt);
fflush(stdout);
}
if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
app_error(&quot;fgets error&quot;);
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
void eval(char *cmdline) {
char *argv[MAXARGS];
char buf[MAXLINE];
int bg;
pid_t pid;
strcpy(buf, cmdline);
bg = parseline(buf, argv);
if (argv[0] == NULL) {
return;
}
if (bg != -1 && !builtin_cmd(argv)) {
//处理addjob与子进程结束的竞争条件
mask_signal(SIG_BLOCK, SIGCHLD);
if ((pid = fork()) == 0) {
//child
mask_signal(SIG_UNBLOCK, SIGCHLD);
if (setpgid(0, 0) < 0) { /* put the child in a new process group */
unix_error(&quot;eval: setpgid failed&quot;);
}
if (execve(argv[0], argv, environ) < 0) {
printf(&quot;%s: Command not found.\n&quot;, argv[0]);
exit(0);
}
} else {
//addjob
if (!bg) {
//fg
addjob(jobs, pid, FG, cmdline);
} else {
//bg
addjob(jobs, pid, BG, cmdline);
}
mask_signal(SIG_UNBLOCK, SIGCHLD); /* 添加完成，解除阻塞 */
//wait foreground to stop
if (!bg) {
waitfg(pid);
} else {
printf(&quot;[%d] (%d) %s&quot;, pid2jid(pid), pid, cmdline);
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
int parseline(const char *cmdline, char **argv) {
static char array[MAXLINE]; /* holds local copy of command line */
char *buf = array; /* ptr that traverses command line */
char *delim; /* points to first space delimiter */
int argc; /* number of args */
int bg; /* background job? */
strcpy(buf, cmdline);
buf[strlen(buf) - 1] = ' '; /* replace trailing '\n' with space */
while (*buf && (*buf == ' ')) /* ignore leading spaces */
buf++;
/* Build the argv list */
argc = 0;
if (*buf == '\'') {
buf++;
delim = strchr(buf, '\'');
} else {
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
} else {
delim = strchr(buf, ' ');
}
}
argv[argc] = NULL;
if (argc == 0) /* ignore blank line */
return 1;
/* should the job run in the background? */
if ((bg = (*argv[argc - 1] == '&')) != 0) {
argv[--argc] = NULL;
}
return bg;
}
/* 
* builtin_cmd - If the user has typed a built-in command then execute
*    it immediately.  
*/
int builtin_cmd(char **argv) {
if (!strcmp(argv[0], &quot;quit&quot;)) {
exit(0);
}
if (!strcmp(argv[0], &quot;&&quot;)) {
return 1;
}
if (!strcmp(argv[0], &quot;echo&quot;)) {
do_echo(argv);
return 1;
}
if (!strcmp(&quot;bg&quot;, argv[0]) || !(strcmp(&quot;fg&quot;, argv[0]))) {
printf(&quot;bg/fg \n&quot;);
do_bgfg(argv);
return 1;
}
if (!strcmp(&quot;jobs&quot;, argv[0])) {
listjobs(jobs);
return 1;
}
return 0; /* not a builtin command */
}
/* 
* do_bgfg - Execute the builtin bg and fg commands
*/
void do_bgfg(char **argv) {
char* id = argv[1];
struct job_t *job;
//获得jobid
int jobid;
if (id[0] == '%') {
jobid = atoi(id += sizeof(char));
}
if ((job = getjobjid(jobs, jobid)) == NULL) {
printf(&quot;job is not exist&quot;);
return;
}
if (!strcmp(argv[0], &quot;bg&quot;)) {
//bg task
job->state = BG;
//唤醒job
send_signal(-1 * job->pid, SIGCONT);
} else if (!strcmp(argv[0], &quot;fg&quot;)) {
job->state = FG;
send_signal(-1 * job->pid, SIGCONT);
waitfg(job->pid);
}
return;
}
/* 
* waitfg - Block until process pid is no longer the foreground process
*/
void waitfg(pid_t pid) {
while (pid == fgpid(jobs)) {
sleep(0);
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
void sigchld_handler(int sig) {
pid_t pid;
int status, child_sig;
while ((pid = waitpid(-1, &status, WUNTRACED | WNOHANG)) > 0) {
printf(&quot;Handler child %d\n&quot;, (int) pid);
sigint_handler(sig);
if (
WIFSTOPPED(status)) {
/* handle stop signals (this assumes all stop signals are
equivalent to SIGTSTP) */
sigtstp_handler(
WSTOPSIG(status));
} else if (WIFSIGNALED(status)) {
/* 处理异常终止 */
child_sig =
WTERMSIG(status);
if (child_sig == SIGINT)
sigint_handler(child_sig);
else
unix_error(&quot;sigchld_handler: uncaught signal/n&quot;);
} else
deletejob(jobs, pid); /* remove the job under normal conditions */
}
return;
}
/* 
* sigint_handler - The kernel sends a SIGINT to the shell whenver the
*    user types ctrl-c at the keyboard.  Catch it and send it along
*    to the foreground job.  
*/
void sigint_handler(int sig) {
pid_t pid = fgpid(jobs);//get fgid
int jid = pid2jid(pid);
if (pid != 0) {
printf(&quot;Job %d terminated by signal: Interrupt&quot;, jid);
deletejob(jobs, pid);
send_signal(-pid, sig);
}
return;
}
/*
* sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
*     the user types ctrl-z at the keyboard. Catch it and suspend the
*     foreground job by sending it a SIGTSTP.  
*/
void sigtstp_handler(int sig) {
printf(&quot;stop sig\n&quot;);
pid_t pid = fgpid(jobs); /* get pid of the fg job */
int jid = pid2jid(pid);
/* if there is a fg job, send it the signal */
if (pid != 0) {
printf(&quot;Job [%d] (%d) stopped by signal %d/n&quot;, jid, pid, sig);
(*(getjobpid(jobs, pid))).state = ST; /* job's state is &quot;stopped&quot; */
send_signal(-pid, sig); /* forward the signal to the process group */
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
clearjob(&jobs);
}
/* maxjid - Returns largest allocated job ID */
int maxjid(struct job_t *jobs) {
int i, max = 0;
for (i = 0; i < MAXJOBS; i++)
if (jobs.jid > max)
max = jobs.jid;
return max;
}
/* addjob - Add a job to the job list */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline) {
int i;
if (pid < 1)
return 0;
for (i = 0; i < MAXJOBS; i++) {
if (jobs.pid == 0) {
jobs.pid = pid;
jobs.state = state;
jobs.jid = nextjid++;
if (nextjid > MAXJOBS)
nextjid = 1;
strcpy(jobs.cmdline, cmdline);
if (verbose) {
printf(&quot;Added job [%d] %d %s\n&quot;, jobs.jid, jobs.pid,
jobs.cmdline);
}
return 1;
}
}
printf(&quot;Tried to create too many jobs\n&quot;);
return 0;
}
/* deletejob - Delete a job whose PID=pid from the job list */
int deletejob(struct job_t *jobs, pid_t pid) {
int i;
if (pid < 1)
return 0;
for (i = 0; i < MAXJOBS; i++) {
if (jobs.pid == pid) {
clearjob(&jobs);
nextjid = maxjid(jobs) + 1;
return 1;
}
}
return 0;
}
/* fgpid - Return PID of current foreground job, 0 if no such job */
pid_t fgpid(struct job_t *jobs) {
int i;
for (i = 0; i < MAXJOBS; i++)
if (jobs.state == FG)
return jobs.pid;
return 0;
}
/* getjobpid  - Find a job (by PID) on the job list */
struct job_t *getjobpid(struct job_t *jobs, pid_t pid) {
int i;
if (pid < 1)
return NULL;
for (i = 0; i < MAXJOBS; i++)
if (jobs.pid == pid)
return &jobs;
return NULL;
}
/* getjobjid  - Find a job (by JID) on the job list */
struct job_t *getjobjid(struct job_t *jobs, int jid) {
int i;
if (jid < 1)
return NULL;
for (i = 0; i < MAXJOBS; i++)
if (jobs.jid == jid)
return &jobs;
return NULL;
}
/* pid2jid - Map process ID to job ID */
int pid2jid(pid_t pid) {
int i;
if (pid < 1)
return 0;
for (i = 0; i < MAXJOBS; i++)
if (jobs.pid == pid) {
return jobs.jid;
}
return 0;
}
/* listjobs - Print the job list */
void listjobs(struct job_t *jobs) {
int i;
for (i = 0; i < MAXJOBS; i++) {
if (jobs.pid != 0) {
printf(&quot;[%d] (%d) &quot;, jobs.jid, jobs.pid);
switch (jobs.state) {
case BG:
printf(&quot;Running &quot;);
break;
case FG:
printf(&quot;Foreground &quot;);
break;
case ST:
printf(&quot;Stopped &quot;);
break;
default:
printf(&quot;listjobs: Internal error: job[%d].state=%d &quot;, i,
jobs.state);
}
printf(&quot;%s&quot;, jobs.cmdline);
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
void usage(void) {
printf(&quot;Usage: shell [-hvp]\n&quot;);
printf(&quot;   -h   print this message\n&quot;);
printf(&quot;   -v   print additional diagnostic information\n&quot;);
printf(&quot;   -p   do not emit a command prompt\n&quot;);
exit(1);
}
/*
* unix_error - unix-style error routine
*/
void unix_error(char *msg) {
fprintf(stdout, &quot;%s: %s\n&quot;, msg, strerror(errno));
exit(1);
}
/*
* app_error - application-style error routine
*/
void app_error(char *msg) {
fprintf(stdout, &quot;%s\n&quot;, msg);
exit(1);
}
/*
* Signal - wrapper for the sigaction function
*/
handler_t *Signal(int signum, handler_t *handler) {
struct sigaction action, old_action;
action.sa_handler = handler;
sigemptyset(&action.sa_mask); /* block sigs of type being handled */
action.sa_flags = SA_RESTART; /* restart syscalls if possible */
if (sigaction(signum, &action, &old_action) < 0)
unix_error(&quot;Signal error&quot;);
return (old_action.sa_handler);
}
/*
* sigquit_handler - The driver program can gracefully terminate the
*    child shell by sending it a SIGQUIT signal.
*/
void sigquit_handler(int sig) {
printf(&quot;Terminating after receipt of SIGQUIT signal\n&quot;);
exit(1);
}
/*
* echo function
*/
void do_echo(char **argv) {
int i = 0;
while (1) {
if (argv[i + 1] != NULL) {
printf(&quot;%s &quot;, argv[i + 1]);
i++;
} else {
break;
}
}
}
/*
#  * mask the indicated signal according to &quot;how&quot;
#  */
sigset_t mask_signal(int how, int sig) {
sigset_t signals, result;
if (sigemptyset(&signals) < 0)
unix_error(&quot;mask_signal: sigemptyset failed&quot;);
if (sigaddset(&signals, sig) < 0)
unix_error(&quot;mask_signal: sigaddset failed&quot;);
if (sigprocmask(how, &signals, &result) < 0)
unix_error(&quot;mask_signal: sigprocmask failed&quot;);
return result;
}
/*
* error-handling wrapper for kill()
*/
int send_signal(pid_t pid, int sig) {
if (kill(pid, sig) < 0) {
/* fail silently if pid doesn't exist -- this keeps handlers that
are called too close together from complaining */
if (errno != ESRCH)
unix_error(&quot;send_signal: kill failed&quot;);
}
return 0;
}