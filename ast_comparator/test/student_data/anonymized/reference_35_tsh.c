#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>

#include <sys/stat.h>


/* Misc manifest constants */
#define MAXLINE 1024 /* max line size */
#define MAXARGS 128 /* max args on a command line */
#define MAXJOBS 16 /* max jobs at any point in time */
#define MAXJID 1<<16 /* max job ID */

/* Job states */
#define UNDEF 0 /* undefined */
#define FG 1 /* running in foreground */
#define BG 2 /* running in background */
#define ST 3 /* stopped */

/* 
* Jobs states: FG (foreground), BG (background), ST (stopped)
* Job state transitions and enabling actions:
* FG -> ST : ctrl-z
* ST -> FG : fg command
* ST -> BG : bg command
* BG -> FG : fg command
* At most 1 job can be in the FG state.
*/

/* Global variables */
extern char **environ; /* defined in libc */
char prompt[] = “tsh> ”; /* command line prompt (DO NOT CHANGE) */
int verbose = 0; /* if true, print additional output */
int nextjid = 1; /* next job ID to allocate */
char sbuf[MAXLINE]; /* for composing sprintf messages */

struct job_t { /* The job struct */
pid_t pid; /* job PID */
int jid; /* job ID [1, 2, …] */
int state; /* UNDEF, BG, FG, or ST */
char cmdline[MAXLINE]; /* command line */
};
struct job_t jobs[MAXJOBS]; /* The job list */
/* End global variables */


/* Function prototypes */
void eval(char *cmdline);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

/* Here are helper routines that we’ve provided for you */
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
void listjobs(struct job_t *jobs, int output_fd);

void usage(void);
void unix_error(char *msg);
void app_error(char *msg);
typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);
int builtin_cmd(char** apszBuffer, int state);
void waitfg(pid_t pid, int output_fd);

int main(int argc, char **argv) 
{
char c;
char cmdline[MAXLINE];
int emit_prompt = 1; /* emit prompt (default) */

/* Redirect stderr to stdout (so that driver will get all output
* on the pipe connected to stdout) */
dup2(1, 2);

/* Parse the command line */
while ((c = getopt(argc, argv, “hvp”)) != EOF) {
switch © {
case ‘h’: /* print help message */
usage();
break;
case 'v’: /* emit additional diagnostic info */
verbose = 1;
break;
case 'p’: /* don’t print a prompt */
emit_prompt = 0; /* handy for automatic testing */
break;
default:
usage();
}
}

/* Install the signal handlers */

/* These are the ones you will need to implement */
Signal(SIGINT, sigint_handler); /* ctrl-c */
Signal(SIGTSTP, sigtstp_handler); /* ctrl-z */
Signal(SIGCHLD, sigchld_handler); /* Terminated or stopped child */
Signal(SIGTTIN, SIG_IGN);
Signal(SIGTTOU, SIG_IGN);

/* This one provides a clean way to kill the shell */
Signal(SIGQUIT, sigquit_handler);

/* Initialize the job list */
initjobs(jobs);

/* Execute the shell’s read/eval loop */
while (1) {

/* Read command line */
if (emit_prompt) {
printf(“%s”, prompt);
fflush(stdout);
}
if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
app_error(“fgets error”);
if (feof(stdin)) { /* End of file (ctrl-d) */
fflush(stdout);
fflush(stderr);
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
* the foreground, wait for it to terminate and then return. Note:
* each child process must have a unique process group ID so that our
* background children don’t receive SIGINT (SIGTSTP) from the kernel
* when we type ctrl-c (ctrl-z) at the keyboard. 
*/
void eval(char *cmdline) 
{
int nState = 0;
int job_cnt=0, i=0, k=0,x=0, stats=0;
int loop = 0;
int ifd, ofd;
char *apszBuffer[MAXARGS] = {0x00};
pid_t pid;
sigset_t mask; /* signal mask */
/*
* 입력받은 cmdline을 명령어를 parse할수 있도록 token한다.
*/
/* 들어온 명령어를 쪼개어 버퍼에 나누어 저장한다.
* 리턴값의 nState 는 foreground 와 background 를 구분한다.
*/
nState = parseline(cmdline, (char**)apszBuffer);

/* 입력받은 apszBuffer 에 ’<'나 ’>'가 존재 하는지
* 검사하고 찾앗다면 그 인덱스 주소를 k와 x에 저장한다.
*/
while(apszBuffer[i] !=NULL)
{
loop = strlen(apszBuffer[i]);
while(loop–)
{
if(! strcmp(apszBuffer[i],“<”))
{
strcpy(apszBuffer[i], “ ”);
k = i;
stats = i;
}
if(! strcmp(apszBuffer[i], “>”))
{
strcpy(apszBuffer[i], “ ”);
x = i;
stats += i*10;
}
}
i++;
}

/* builtin_cmd인지 확인하고 아무입력이 없는건지 확인한다. */
if( strcmp( cmdline,“\n” ) )
{

/*builtin_cmd 함수에 state를 넘긴다. 인덱스를 의미한다.*/
if( !builtin_cmd( (char**) apszBuffer, stats ) 
&& strncmp( cmdline, “ ”, 1 ) )
{

if(sigemptyset(&mask)<0) 
/* mask가 가리키는 시그널 집합을 초기화한다.*/
unix_error(“sigemptyset error”);
if(sigaddset(&mask, SIGCHLD))
/* mask에 SIGCHLD, SIGINT, SIGTSTP를 등록한다. */
unix_error(“sigaddset error”);
if(sigaddset(&mask, SIGINT))
unix_error(“sigaddset error”);
if(sigaddset(&mask, SIGTSTP))
unix_error(“sigaddset error”);
/* mask에 등록된 시그널을 BLOCK 한다. */
if(sigprocmask(SIG_BLOCK, &mask, NULL) < 0 )
unix_error(“sigprocmask error”);
/* fork의 리턴값으로 부모와 자식프로세스가 하는일을 구분하고
* 실패시 “fork failed"라는 문자열을 출력하도록 하였다.
*/
if( 0 == ( pid = fork() ) )
{
sigprocmask(SIG_UNBLOCK, &mask, NULL);
/* 프로세스 그룹아이디를 바꿔줌 */
if( setpgid(0,0) < 0)
unix_error("setpgid error”);
/* 자식프로세스 실행부
* 명령어가 올바르게 작동한다면 execv라는
* 함수는 리턴하지 않고 종료된다.
* 실행 실패시 오류메세지 출력후 종료
*/
/* ’<'존재하면 ’<'뒤에 존재하는 파일을 표준입력으로 만든다. */
if(k){
if( (ifd = open( apszBuffer[k+1],O_RDWR)) <0)
unix_error(“ifd open error”); 
if(dup2(ifd,0)<0)
unix_error(“ifd dup error”);
close(ifd);
}
/* ’>'존재하면 ’>'뒤에 존재하는 파일을 표준출력으으로 만든다. */
if(x)
{ 
if( (ofd = open( apszBuffer[x+1],
O_RDWR|O_CREAT| O_TRUNC, S_IRWXU | S_IRWXG | S_IRWXO)) <0 )
unix_error(“ofd open error”);
if( dup2(ofd, 1) <0 )
unix_error(“ofd dup error”);
close(ofd);
}

if( execvp( apszBuffer[0], apszBuffer) < 0)
{
printf( “\n%s: Command not found.\n”, apszBuffer[0] );
}
exit( 0 );

}else if( pid > 0 ){
/* 부모 프로세스 실행부
* job리스트에 추가시킨다.
*/
addjob(jobs,pid,(nState==1 ? BG : FG),cmdline);
if( sigprocmask(SIG_UNBLOCK, &mask, NULL)<0 )
unix_error(“sigprocmask error”);
if( !nState )
{
/* forground 모드로 실행시 자식프로세스의 종료때까지 
* 부모프로세스가 기다리게 하기 위해서 waitpid라는
* 프로세스를 사용하였씀
*/

waitfg( pid, 1);

}else{
/* background 모드로 실행시 자식프로세스를 기다리지 않고
* 명령대기 모드로 돌아간다. 또한 자식프로세스가 
* background 모드로 돌아간다는 것을 job목록에 추가시켰다는
* 메세지를 출력해주어야 한다.maxjid함수에서 job리스트에
* 추가 되어 있는 갯수를 리턴해주게된다.
*/
job_cnt = maxjid(jobs);
printf(“[%d] (%d) %s”, jobs[job_cnt-1].jid, jobs[job_cnt-1].pid, cmdline);
} 
}else{
/* fork 실패시 오류 메세지를 출력한다. */
printf(“\nfork failed\n”);
}
}
}
return;
}

/*
* waitfg - Block until process pid is no longer the foreground process
*/
void waitfg(pid_t pid, int output_fd)
{
struct job_t *j = getjobpid(jobs,pid);
char buf[MAXLINE];

/*
* The FG job has already completed and been reaped by the handler
*/
if(!j)
return ;

/* Wait for process pid to longer be the foreground process.
* Note : using pause() instead of sleep() would introduce a race
* that could cause us to miss the signal
*/
while(j->pid == pid && j->state == FG)
sleep(1);

if(verbose)
{
memset(buf, ’\0’, MAXLINE);
sprintf(buf,“waitfg : Process(%d) no longer the fg process:q\n”,pid );
if(write(output_fd, buf, strlen(buf)<0) )
{
fprintf(stderr, “Error writing to file\n”);
exit(1);
}
}
return;
}

int builtin_cmd( char** apszBuffer, int state)
{
/* builtin_cmd명령어이면 리턴값이 1 아니면 리턴값이 0이다. 
* builtin_cmd 명령어들을 저장하는 char 배열이다.
*/
char* command[10] = {“quit”,“jobs”,“bg”,“fg”};
pid_t pid;
int jid, ifd, ofd, i=0, mofd;
struct job_t *temp;
/* 기존의 표준 입출력 디스크립터를 백업해준다. */
if( dup2(STDOUT_FILENO, mofd)<0)
unix_error(“mofd dup error”);
for( i = 0; command[i] != NULL ; i++)
{
if( !strcmp(apszBuffer[0], command[i]) )
{
if(state)
{
/* 넘어온 인자인 state는 십의 자리는 ’>'의 인덱스를
* 일의 자리는 ’<'의 인덱스를 뜻한다.
*/
if( state%10 )
{
if( (ifd = open( apszBuffer[(state%10)+1],O_RDWR)) <0)
unix_error(“ifd open error”);
if( dup2(ifd,0) <0 )
unix_error(“ifd dup error”);
}
if(state/10)
{ 
if( (ofd = open( apszBuffer[(state/10)+1],
O_RDWR|O_CREAT| O_TRUNC, S_IRWXU | S_IRWXG | S_IRWXO)) < 0)
unix_error(“ofd open error”);
if( dup2(ofd, 1) <0 )
unix_error(“ofd dup open error”);
}
}
break;

}
}
/* quit와 명령어가 같은지 판별후 같으면 종료시킨다. */
if(!strcmp(apszBuffer[0],command[0]))
{
exit(0);
}else if(!strcmp(apszBuffer[0],command[1]))
/* jobs와 명령어가 같은지 판별후 같으면 listjobs을 출력 */
{
listjobs(jobs,STDOUT_FILENO); 
/* 다시 표준 입출력 디스크립터를 원상복구 해준다. */
close(1);
dup2(mofd,1);
return 1;
}else if( !strcmp(apszBuffer[0],command[2]) )
{
/* jid인경우 jobs에서 pid를 받아온다. */
if(apszBuffer[1][0] == ’%’)
{
jid = atoi(&apszBuffer[1][1]);
temp = getjobjid(jobs, jid);

}else
{
/* pid인경우 그냥 사용한다. */
pid = atoi(apszBuffer[1]);
temp = getjobpid( jobs, pid );
} 
/* 다시 수행하도록 SIGCONT를 보낸다. */
if(kill( -(temp->pid), SIGCONT)<0 )
{
printf(“\n%d의 프로세스 SIGCONT 시그널 오류”, pid);
}
/* 상태를 BG모드로 바꿔준다. */
temp->state=BG;
printf(“[%d] (%d) %s”, temp->jid, temp->pid, temp->cmdline);
return 1;

}else if( !strcmp(apszBuffer[0],command[3]) )
{
/* jid인경우 jobs에서 pid를 받아온다. */
if(apszBuffer[1][0] == ’%’)
{
jid = atoi(&apszBuffer[1][1]);
temp = getjobjid(jobs, jid);

}else
{
/* pid인경우 그냥 사용한다. */
pid = atoi(apszBuffer[1]);
temp = getjobpid( jobs, pid );
} 
/* 다시 수행하도록 SIGCONT를 보낸다. */
if(kill(-(temp->pid), SIGCONT)<0 )
{
printf(“\n%d의 프로세스 SIGCONT 시그널 오류”, pid);
}
/* 상태를 FG모드로 바꿔준다. */
temp->state=FG;

return 1;
}
return 0;
}

/* 
* parseline - Parse the command line and build the argv array.
* 
* Characters enclosed in single quotes are treated as a single
* argument. Return true if the user has requested a BG job, false if
* the user has requested a FG job. 
*/
int parseline(const char *cmdline, char **argv) 
{

static char array[MAXLINE]; /* holds local copy of command line */
char *buf = array; /* ptr that traverses command line */
char *delim; /* points to first space delimiter */
int argc; /* number of args */
int bg; /* background job? */

strcpy(buf, cmdline);
buf[strlen(buf)-1] = ’ ’; /* replace trailing ’\n’ with space */
while (*buf && (*buf == ’ ’)) /* ignore leading spaces */
buf++;

/* Build the argv list */
argc = 0;
if (*buf == ’'’) {
buf++;
delim = strchr(buf, ’'’);
}
else {
delim = strchr(buf, ’ ’);
}

while (delim) {
argv[argc++] = buf;
*delim = ’\0’;
buf = delim + 1;
while (*buf && (*buf == ’ ’)) /* ignore spaces */
buf++;

if (*buf == ’'’) {
buf++;
delim = strchr(buf, ’'’);
}
else {
delim = strchr(buf, ’ ’);
}
}

argv[argc] = NULL;

if (argc == 0) /* ignore blank line */
return 1;

/* should the job run in the background? */
if ((bg = (*argv[argc-1] == ’&’)) != 0)
argv[–argc] = NULL;

return bg;

}


/*****************
* Signal handlers
*****************/

/* 
* sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
* a child job terminates (becomes a zombie), or stops because it
* received a SIGSTOP or SIGTSTP signal. The handler reaps all
* available zombie children, but doesn’t wait for any other
* currently running children to terminate. 
*/


void sigchld_handler(int sig)
{
int status;
pid_t child_pid;
/* 자식프로세스의 pid를 받아온다. */
while((child_pid=waitpid(-1,&status,WNOHANG|WUNTRACED))>0){
/* WIFSIGNALED(status) 자식프로세스가 신호가 없기
* 때문에 종료되었을시 참은 반환한다.
*/
if(WIFSIGNALED(status)){
/* WTERMSIG(status)는 WIFSIGNALED(status)가 0이 아닌경우
* 자식프로세스를 종료하게끔 만든 신호의 숫자를 반환
* 종료된 자식프로세스는 joblist에서 삭제 해준다.
*/
printf(“Job [%d] (%d) terminated by signal %d \n”,pid2jid(child_pid),
child_pid,WTERMSIG(status));
deletejob(jobs,child_pid);
}
/* 자식프로세스가 정상적으로 종료시에 joblist에서 삭제해준다. */
else if(WIFEXITED(status))
deletejob(jobs,child_pid);
/* 자식 프로세스가 정지시 상태를 ST로 바꿔주고 그 결과를 출력해준다. */
else if(WIFSTOPPED(status)){
printf(“Job [%d] (%d) stopped by signal %d \n”,
pid2jid(child_pid),child_pid,WSTOPSIG(status));
/* 자식프로세스의 상태를 ST 상태로 변환시켜주기 위해
* 임시적으로 temp라는 포인터를 선언
*/
struct job_t *temp=getjobpid(jobs,child_pid);
temp->state=ST;
}
}
}


/* 
* sigint_handler - The kernel sends a SIGINT to the shell whenver the
* user types ctrl-c at the keyboard. Catch it and send it along
* to the foreground job. 
*/
void sigint_handler(int sig) 
{
pid_t pid;
/* 현재 FG모드로 실행되고 있는 프로세스의 pid를 받아온다. */
if( ( pid = fgpid(jobs) ) > 0 )
{
/* 현재 FG모드로 실행되고 있는 프로세스를 KILL한다. */
if(kill( -pid, sig) < 0 )
printf(“\nsigint kill error”);
}

return;
}
/*
* sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
* the user types ctrl-z at the keyboard. Catch it and suspend the
* foreground job by sending it a SIGTSTP. 
*/
void sigtstp_handler(int sig) 
{
pid_t pid;

/* 현재 FG모드로 실행되고 있는 프로세스의 pid를 받아온다. */
if( ( pid = fgpid(jobs) ) > 0 )
{
/* 현재 FG모드로 실행되고 있는 프로세스를 KILL한다. */
if(kill( -pid, sig) <0)
{
printf(“\nsigtstp kill error”);
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
job->cmdline[0] = ’\0’;
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
printf(“Added job [%d] %d %s\n”, jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
}
return 1;
}
}
printf(“Tried to create too many jobs\n”);
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

/* getjobpid - Find a job (by PID) on the job list */
struct job_t *getjobpid(struct job_t *jobs, pid_t pid) {
int i;

if (pid < 1)
return NULL;
for (i = 0; i < MAXJOBS; i++)
if (jobs[i].pid == pid)
return &jobs[i];
return NULL;
}

/* getjobjid - Find a job (by JID) on the job list */
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
void listjobs(struct job_t *jobs, int output_fd) 
{
int i;
char buf[MAXLINE];

for (i = 0; i < MAXJOBS; i++) {
memset(buf, ’\0’, MAXLINE);
if (jobs[i].pid != 0) {
sprintf(buf, “[%d] (%d) ”, jobs[i].jid, jobs[i].pid);
if(write(output_fd, buf, strlen(buf)) < 0) {
fprintf(stderr, “Error writing to output file\n”);
exit(1);
}
memset(buf, ’\0’, MAXLINE);
switch (jobs[i].state) {
case BG:
sprintf(buf, “Running ”);
break;
case FG:
sprintf(buf, “Foreground ”);
break;
case ST:
sprintf(buf, “Stopped ”);
break;
default:
sprintf(buf, “listjobs: Internal error: job[%d].state=%d ”,
i, jobs[i].state);
}
if(write(output_fd, buf, strlen(buf)) < 0) {
fprintf(stderr, “Error writing to output file\n”);
exit(1);
}
memset(buf, ’\0’, MAXLINE);
sprintf(buf, “%s”, jobs[i].cmdline);
if(write(output_fd, buf, strlen(buf)) < 0) {
fprintf(stderr, “Error writing to output file\n”);
exit(1);
}
}
}
if(output_fd != STDOUT_FILENO)
close(output_fd);
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
printf(“Usage: shell [-hvp]\n”);
printf(“ -h print this message\n”);
printf(“ -v print additional diagnostic information\n”);
printf(“ -p do not emit a command prompt\n”);
exit(1);
}

/*
* unix_error - unix-style error routine
*/
void unix_error(char *msg)
{
fprintf(stdout, “%s: %s\n”, msg, strerror(errno));
exit(1);
}

/*
* app_error - application-style error routine
*/
void app_error(char *msg)
{
fprintf(stdout, “%s\n”, msg);
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
unix_error(“Signal error”);
return (old_action.sa_handler);
}

/*
* sigquit_handler - The driver program can gracefully terminate the
* child shell by sending it a SIGQUIT signal.
*/
void sigquit_handler(int sig) 
{
printf(“Terminating after receipt of SIGQUIT signal\n”);
exit(1);
}