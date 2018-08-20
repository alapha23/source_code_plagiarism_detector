// Taken from: https://raw.githubusercontent.com/eric-ha/ShellLab/master/ShellLabFinal/tsh.cc

// 
// tsh - A tiny shell program with job control
// ****FINAL****
// Matthew Moore  mamo8125
// Eric Ha        erha5113

using namespace std;

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <string>

#include "globals.h"
#include "jobs.h"
#include "helper-routines.h"

//
// Needed global variable definitions
//

static char prompt[] = "tsh> ";
int verbose = 0;

int nextjid = 1;  

//
// You need to implement the functions eval, builtin_cmd, do_bgfg,
// waitfg, sigchld_handler, sigstp_handler, sigint_handler
//
// The code below provides the "prototypes" for those functions
// so that earlier code can refer to them. You need to fill in the
// function bodies below.
// 

/////////////BREAK

/* Here are the functions that you will implement */
void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);




/*error-handling wrapper funtion -by yzf*/
void Sigemptyset(sigset_t *mask);
void Sigaddset(sigset_t *mask,int sign);
void Sigprocmask(int how,sigset_t *mask,sigset_t *oldmask);
pid_t Fork(void);
void Execve(char *filename,char *argv[],char *envp[]);
void Setpgid(pid_t pid,pid_t gpid);
//void kill(pid_t pid,int sig);
/* Here are helper routines that we've provided for you */
//int parseline(const char *cmdline, char **argv);
//void sigquit_handler(int sig);


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
    Signal(SIGINT,  sigint_handler);   /* ctrl-c */
    Signal(SIGTSTP, sigtstp_handler);  /* ctrl-z */
    Signal(SIGCHLD, sigchld_handler);  /* Terminated or stopped child */

    /* This one provides a clean way to kill the shell */
    Signal(SIGQUIT, sigquit_handler);

    /* Initialize the job list */
    initjobs(jobs);

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
        if (feof(stdin))   /* End of file (ctrl-d) */
        {
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
    char *argv[MAXLINE];    /*argument list of execve()*/
    char buf[MAXLINE];      /*hold modified commend line*/
    int bg;                 /*should the job run in bg or fg?*/
    // The 'bg' variable is TRUE if the job should run
    // in background mode or FALSE if it should run in FG

    pid_t pid;
    sigset_t mask;          /*mask for signal*/

    stpcpy(buf,cmdline);
    bg = parseline(buf,argv);
    sigemptyset(&mask);
    sigaddset(&mask,SIGCHLD);

    if(argv[0]==NULL){
        return;     /*ignore empty line*/
    }

    if (!builtin_cmd(argv)) {        /* If user input is not a built in command, fork(), not a build in cmd */
    
    /* Parent blocks SIGCHLD signal temporarily */
    sigprocmask(SIG_BLOCK, &mask, 0);  /*block the SIGCHLD signal*/
    
    if ((pid = fork()) < 0) {   /* Child runs user job */
        printf("fork(): forking error\n");
        return;
    }
    
    if (pid == 0) {
        setpgid(0,0);               /* Change child process group id, puts the child in a new process group */
        
        if (execvp(argv[0], argv) < 0) {
        printf("%s: Command not found. \n", argv[0]);
        exit(0);
        }
        
    } else {
        /* Parent waits for fg job to terminate */
        if (bg == 1) {
        addjob(jobs, pid, BG, cmdline);     /* adds to joblist as bg if it is bg */
        printf("[%d] (%d) %s", pid2jid(pid), pid, cmdline);
        } else {
        addjob(jobs, pid, FG, cmdline);     /* adds to joblist as fg, if !bg */
        }
        
        sigprocmask(SIG_UNBLOCK, &mask, 0);     /* unblock the SIGCHLD */
        waitfg(pid);  //do in background or foreground
    }
    }
    return;
}

/*
 * builtin_cmd - If the user has typed a built-in command then execute
 *    it immediately.
 */
int builtin_cmd(char **argv)
{
    //strcmp compares the users type in with following commands:quit, &, bg, fg, jobs
    if(!strcmp(argv[0],"quit"))
        exit(0);
    if(!strcmp(argv[0],"&"))
        return 1;
    if(!strcmp(argv[0],"bg")||!strcmp(argv[0],"fg")){
        do_bgfg(argv);
        return 1;
    }
    if(!strcmp(argv[0],"jobs")){
        listjobs(jobs);
        return 1;
    }

    return 0;     /* not a builtin command */
}

/*
 * do_bgfg - Execute the builtin bg and fg commands
 */
void do_bgfg(char **argv) 
{
    int jid;
    int pid;
    char *id = argv[1];
    struct job_t *job;
    
    if (id != NULL) {     /* bg or fg has the argument (passed to it) */
    
    if (id[0] == '%' && isdigit(id[1])) {   /* argument is a job id */
        
        jid = atoi(&id[1]);
        
        if (!(job = getjobjid(jobs, jid))) {
        printf("%s: No such job\n", id);
        return;
        }
        
    } else if (isdigit(*argv[1])) { /* the argument is a pid is a digit number */
        
        pid = atoi(&id[0]);
        
        if (!(job = getjobpid(jobs, pid))) { //not a process id
        printf("(%s): No such process\n", argv[1]);
        return;
        }
        
    } else {
        printf("%s: argument must be a PID or %%jobid\n", argv[0]);
        return;
    }
    
    } else {
    printf("%s command requires PID or %%jobid argument\n", argv[0]);
    return;
    }
    
    if (job != NULL) {
    pid = job->pid;
    
    if (job->state == ST) {
        if (!strcmp(argv[0], "bg")) { //set job state, do it in bg or fg
        printf("[%d] (%d) %s", job->jid, job->pid, job->cmdline);
        job->state = BG;
        kill(-pid, SIGCONT); //seng the SIGCONT to pid
        }
        
        if (!strcmp(argv[0], "fg")) {
        job->state = FG;
        kill(-pid, SIGCONT);
        waitfg(job->pid);
        }
    }
    
    if (job->state == BG) {
        if (!strcmp(argv[0], "fg")) {
        job->state = FG;
        waitfg(job->pid);
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
    struct job_t *p = getjobpid(jobs, pid);

    while(p->state == FG){
        sleep(1);
    }
    return;
//    waitpid(pid,NULL,0);    /*this is wrong answer ,see the num5 hints*/
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

    pid_t pid;
    int status;
    while((pid = waitpid(-1,&status,WNOHANG | WUNTRACED))>0){
        if(WIFEXITED(status)){  /*process is exited in normal way*/
            deletejob(jobs,pid);
        }
        if(WIFSIGNALED(status)){/*process is terminated by a signal*/
            printf("Job [%d] (%d) terminated by signal %d\n",pid2jid(pid),pid,WTERMSIG(status));
            deletejob(jobs,pid);
        }
        if(WIFSTOPPED(status)){/*process is stop because of a signal*/
            getjobpid(jobs, pid)->state = ST;
        printf("Job [%d] (%d) stopped by signal %d\n",pid2jid(pid),pid,WSTOPSIG(status));
        }
    }
    if(pid < 0 && errno != ECHILD)
    printf("waitpid error: %s\n", strerror(errno));

    return;
}

/*
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.
 */
void sigint_handler(int sig)
{
    pid_t pid = fgpid(jobs);

    if(fgpid(jobs) != 0){
        kill(-pid,SIGINT);
        /*sigchld_handler deletes the job in jobs*/
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

    pid_t pid = fgpid(jobs);

    if(pid!=0 ){
    kill(-pid, SIGTSTP);
    }
    //already stop the job, don’t do it again
    return;
}

/*********************
 * End signal handlers
 *********************/
