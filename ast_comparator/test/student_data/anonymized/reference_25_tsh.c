/*  
 * tsh - A tiny shell program with job control 
 *  
 * <Put your name and login ID here> 
 *----------------- <Name : liang rongtang> ------------------ 
 *----------------- <ID   :0472528 > ------------------ 
 */  
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
#include <sys/dir.h>  
  
/* Misc manifest constants */  
#define MAXLINE    1024   /* max line size */  
#define MAXARGS     128   /* max args on a command line */  
#define MAXJOBS      16   /* max jobs at any point in time */  
#define MAXJID    1<<16   /* max job ID */  
#define MAX_CMD_BUF 50  
#define MAX_CMD_LEN 30  
#define MAX_ARG_LEN 30  
  
/* Job states */  
#define UNDEF 0 /* undefined */  
#define FG 1    /* running in foreground */  
#define BG 2    /* running in background */  
#define ST 3    /* stopped */  
/*Cmd struct*/  
struct cmd_struct  
{  
        char *name;  
        char *cmd_manner;/*The instruction will execute following the cmd_manner*/  
};  
  
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
char prompt[128];    /* command line prompt */  
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
int builtin_cmd(char **argv, int *input_fd, int *output_fd);  
void do_bgfg(char **argv, int output_fd);  
void waitfg(pid_t pid, int output_fd);  
void execute_cd(char **argv);  
void makedir(char **argv);  
void removedir(char **argv);  
void sigchld_handler(int sig);  
void sigtstp_handler(int sig);  
void sigint_handler(int sig);  
  
/* Here are helper routines that we've provided for you */  
int parseline(const char *cmdline, char **argv);  
char *firstTok(const char *space, const char *input,  
    const char *output, const char *pipe);  
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
    Signal(SIGINT,  sigint_handler);   /* ctrl-c */  
    Signal(SIGTSTP, sigtstp_handler);  /* ctrl-z */  
    Signal(SIGCHLD, sigchld_handler);  /* terminated or stopped child */  
    Signal(SIGQUIT, sigquit_handler);  /* so parent can cleanly terminate child*/  
      
    /* Initialize the job list */  
    initjobs(jobs);  
      
    /* Execute the shell's read/eval loop */  
    while (1) {  
          
        /* Read command line */  
        if (emit_prompt) {  
             getcwd(prompt,128);  
            printf("%s>", prompt);                     
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
        fflush(stderr);  
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
        int bg;       /*should the job run in bg or fg*/  
        pid_t pid;  
    sigset_t mask;  
        int in = STDIN_FILENO;  
    int out = STDOUT_FILENO;  
        int *input_fd = ∈  
        int *output_fd=&out;  
    int input=0;  
    int output=0;  
    int argc=0;  
   
  
        bg=parseline(cmdline,argv) ;  
  
        if (argv[0]==NULL)  
       return;                          /*ignore empty line*/  
    while(argv[argc] !=NULL&&*argv[argc] !='\0')  
    {  
         if(*argv[argc]=='<')                   /*is there input I\O redirection*/  
         {  
            if(in!=STDIN_FILENO)           /*illegal to use multiple redirection*/  
        {  
            printf("Error: Ambiguous I/O redirection\n");    
            return;  
        }  
        else  
        {  
            in=open(argv[argc+1],O_RDONLY);    /*open the specific file*/  
            if(in==-1)  
            {  
               printf("Error: %s No such file or directory\n",argv[argc+1]);  
               return;  
            }  
            else  
               input=1;  
        }  
         }  
           
         if(*argv[argc]=='>')       /*is there output I\O redirection*/  
         {  
            if(out!=STDOUT_FILENO)         /*illegal to use multiple redirection*/  
        {  
            printf("Error: Ambiguous I/O redirection\n");     
            return;  
        }  
        else  
        {  
            out=open(argv[argc+1],O_WRONLY|O_CREAT); /*open the specific file*/  
            output=1;  
        }  
        argv[argc]=NULL;  
         }  
         argc++;  
    }  
        if(!builtin_cmd(argv,input_fd,output_fd))      /*if the command a built-in command*/  
    {  
            sigemptyset(&mask);  
            sigaddset(&mask,SIGCHLD);  
            sigprocmask(SIG_BLOCK,&mask,NULL);     /*block signal SIGCHLD*/  
  
            if((pid=fork())==0)  
        {  
               sigprocmask(SIG_UNBLOCK,&mask,NULL);    /*unblock signal SIGCHLD*/  
           setpgid(0,0);                            /*pid is set for the job id*/  
             
           if(input==1)  
           {  
               dup2(in,STDIN_FILENO);     /*job should read file from in instead of STDIN*/  
           }  
           if(output==1)  
           {  
               dup2(out,STDOUT_FILENO);  /*job should write to out in instead of STDOUT*/  
           }  
               if(execve(argv[0],argv,environ)<0 )        
           {  
      
                   printf("%s:Command not found.\n",argv[0]);  
                   exit(0);  
  
               }  
             }  
  
             addjob(jobs,pid,bg+1,cmdline);             /*add job to joblist*/  
             sigprocmask(SIG_UNBLOCK,&mask,NULL);       /*unblock signal SIGCHLD*/  
  
             if(!bg) /*run in foreground*/  
         {   
                waitfg(pid ,*output_fd);        /*wait for foreground job to terminate*/  
             }  
             else /*run in background*/  
         {   
                printf("[%d] (%d) %s",pid2jid(pid),pid,cmdline);  
             }  
        }  
        return;  
}  
  
/*  
 * parseline - Parse the command line and build the argv array. 
 * Return true (1) if the user has requested a BG job, false  
 * if the user has requested a FG job. 
 */  
int parseline(const char *cmdline, char **argv)   
{  
    static char array[MAXLINE]; /* holds local copy of command line */  
    char *buf = array;          /* ptr that traverses command line */  
    char *delim_space;          /* points to first space delimiter */  
    char *delim_in;             /* points to the first < delimiter */  
    char *delim_out;            /* points to the first > delimiter */  
    char *delim_pipe;           /* points to the first | delimiter */  
    char *delim;                /* points to the first delimiter */  
    int argc;                   /* number of args */  
    int bg;                     /* background job? */  
    char *last_space = NULL;    /* The address of the last space  */  
      
    strcpy(buf, cmdline);  
    buf[strlen(buf)-1] = ' ';  /* replace trailing '\n' with space */  
    while (*buf && (*buf == ' ')) /* ignore leading spaces */  
        buf++;  
      
    /* Build the argv list */  
    argc = 0;  
    delim_space = strchr(buf, ' ');  
    delim_in = strchr(buf, '<');  
    delim_out = strchr(buf, '>');  
    delim_pipe = strchr(buf, '|');  
    while ((delim = firstTok(delim_space, delim_in, delim_out, delim_pipe))) {  
        if(delim == delim_space) {  
            argv[argc++] = buf;  
            *delim = '\0';  
            last_space = delim;  
        } else if(delim == delim_in) {  
            if((last_space && last_space != (delim - 1)) || (!last_space)) {  
                argv[argc++] = buf;  
                *delim = '\0';  
            }  
            argv[argc++] = "<";  
            last_space = 0;  
        } else if(delim == delim_out) {  
            if((last_space && last_space != (delim - 1)) || (!last_space)) {  
                argv[argc++] = buf;  
                *delim = '\0';  
            }  
            argv[argc++] = ">";  
            last_space = 0;  
        } else if(delim == delim_pipe) {  
            if((last_space && last_space != (delim - 1)) || (!last_space)) {   
                argv[argc++] = buf;  
                *delim = '\0';  
            }  
            argv[argc++] = "|";  
            last_space = 0;  
        }  
        buf = delim + 1;  
        while (*buf && (*buf == ' ')) /* ignore spaces */  
            buf++;  
        delim_space = strchr(buf, ' ');  
        delim_in = strchr(buf, '<');  
        delim_out = strchr(buf, '>');  
        delim_pipe = strchr(buf, '|');  
    }  
    argv[argc] = NULL;  
      
    if (argc == 0)  /* ignore blank line */  
        return 1;  
      
    /* should the job run in the background? */  
    if ((bg = (*argv[argc-1] == '&')) != 0)  
        argv[--argc] = NULL;  
  
    return bg;  
}  
  
/* 
 * firstTok - Returns a pointer to the first (lowest addy) of the four pointers 
 *     that isn't NULL. 
 */  
char *firstTok(const char *space, const char *input,  
    const char *output, const char *pipe) {  
    const char *possible[4];  
    unsigned int min;  
    int i = 1, n = 0;  
    if(space == NULL && input == NULL && output == NULL && pipe == NULL)  
        return NULL;  
    if(space != NULL) {  
        possible[n++] = space;  
    }  
    if(input != NULL) {  
        possible[n++] = input;  
    }  
    if(output != NULL) {  
        possible[n++] = output;  
    }  
    if(pipe != NULL) {  
        possible[n++] = pipe;  
    }  
    min = (unsigned)possible[0];  
    for(;i<n;i++) {  
        if(((unsigned)possible[i]) < min)  
            min = (unsigned)possible[i];  
    }  
    return (char *)min;  
}  
  
/*  
 * builtin_cmd - If the user has typed a built-in command then execute 
 *    it immediately.   
 */  
int builtin_cmd(char **argv, int *input_fd, int *output_fd)   
{  
        if(!strcmp(argv[0],"exit"))                 /*exit command, exit directly*/  
            exit(0);  
      
    if(!strcmp(argv[0],"cd")){  
       execute_cd(argv);  
       return 1;  
    }  
  
    if(!strcmp(argv[0],"mkdir")){  
       makedir(argv);  
       return 1;  
    }  
      
    if(!strcmp(argv[0],"rmdir")){  
       removedir(argv);  
       return 1;  
    }  
    if(!strcmp(argv[0],"&"))  
            return 1;  
          
        if(!strcmp(argv[0],"jobs"))                /*jobs command ,list all jobs*/  
    {  
        listjobs(jobs,*output_fd);  
            return 1;  
        }   
  
        if((!strcmp(argv[0],"bg"))||(!strcmp(argv[0],"fg"))) /*bg\fg command */  
    {  
            do_bgfg(argv,*output_fd);  
        return 1;  
        }  
  
    return 0;     /* not a builtin command */  
}  
/* 
*execute_cd - Execute the cd command 
*/  
void execute_cd(char **argv){  
    int index = strlen(argv[1]);  
    int i = 0;  
    char swap[128];  
  
    if(argv[1] == NULL){  
        chdir("/");  
        getcwd(prompt,128);  
    }else if(argv[1][0] == '/'){  
            chdir(argv[1]);   
            getcwd(prompt,128);  
          
        }else if(!strcmp(argv[1],"../")){  
                getcwd(prompt,128);  
                index = strlen(prompt) - 1;  
                while(prompt[index]!='/')  
                   index--;  
                strncpy(swap, prompt, index);             
                chdir(swap);  
                return;  
      
            }else{  
                getcwd(prompt,128);  
                    strcat(prompt,"/");  
                        strcat(prompt,argv[1]);  
                chdir(prompt);  
            }  
    return;  
}  
/* 
*makedir - build the dictory you wanted 
*/  
void makedir(char **argv){  
    int status;  
    status = mkdir(argv[1]);  
    if(status == 0)  
        perror("Directory created!\n");  
    else perror("Unable to create the directory\n");  
}  
/* 
*removedir - remove the dictory you have choosed 
*/  
void removedir(char **argv){  
    int status;  
    status = rmdir(argv[1]);  
    if(status == 0)  
        perror("Directory removed!\n");  
    else  
        perror("Unable to remove the directory!\n");  
}  
/*  
 * do_bgfg - Execute the builtin bg and fg commands 
 */  
  
void do_bgfg(char **argv, int output_fd)   
{   
        struct job_t *job=NULL;  
        if(argv[1]==NULL)         /*bg\fg command without pid\jid*/  
    {  
        printf("%s command requires PID or %%jobid argument\n",argv[0]);  
        return;  
    }  
      
        if(argv[1][0]=='%')        /*%jid is a jid*/  
    {  
       int jid;  
       jid=atoi(argv[1]+1);  
       if((job=getjobjid(jobs, jid))==NULL)  /*is the job found*/  
       {  
           printf("%s NO such job\n",argv[1]);  
           return;  
       }  
    }  
    else{  
       if(!isdigit(*argv[1]))   /*is the pid a digit*/  
       {  
           printf("%s  argument must be a PID or %%jobid\n",argv[0]);  
           return;  
       }  
       pid_t pid=(pid_t)atoi(argv[1]);   
       if((job=getjobpid(jobs, pid))==NULL)  
       {  
           printf("%s NO such process\n",argv[1]);   
           return;  
       }  
    }  
  
    kill(-(job->pid),SIGCONT);   /*send a signal to the process and its child*/  
        if(!strcmp(argv[0],"bg"))  
    {  
            job->state=BG;         /*change state*/  
        printf("[%d] (%d) %s",job->jid,job->pid,job->cmdline);     
        }  
        else  
    {  
            job->state=FG;        /*change state*/  
        waitfg(job->pid,output_fd); /*wait the foreground process to terminate*/  
        }  
    return;  
}  
  
/*  
 * waitfg - Block until process pid is no longer the foreground process 
 */  
void waitfg(pid_t pid, int output_fd)  
{  
        struct job_t *job;  
        job=getjobpid(jobs,pid);  
        while((job!=NULL)&&job->state==FG)  /*if process pid foreground*/  
    {  
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
        struct job_t *job;  
    int status;  
    int jid;  
        pid_t pid;  
        while((pid=waitpid(-1,&status,WNOHANG|WUNTRACED))>0)  
    {  
         job=getjobpid(jobs,pid);  
             if(WIFSTOPPED(status))    /*if the job stopped by signal*/  
         {  
              printf("Job [%d] (%d) stopped by signal %d\n",pid2jid(pid),pid,WSTOPSIG(status));  
              job->state=ST;       /*change job state to stopped*/  
         }  
         else if(WIFEXITED(status))  /*if the job terminated normally*/  
         {  
              deletejob(jobs,pid);  /*delete the job from job list*/  
         }  
         else if(WIFSIGNALED(status)) /*if the job terminated by signal*/  
         {  
              if((jid=job->jid)>0)  
                    printf("Job [%d] (%d) terminated by signal %d\n",jid,pid,WTERMSIG(status));  
              deletejob(jobs,pid);   
         }        
        }  
      
        if(errno!=ECHILD&&errno!=EINTR)  
            unix_error("waitpid error");  
    return;  
}  
  
/*  
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the 
 *    user types ctrl-c at the keyboard.  Catch it and send it along 
 *    to the foreground job.   
 */  
void sigint_handler(int sig)   
{  
        pid_t pid;  
        pid=fgpid(jobs);   /*get the foreground job*/  
    if(!pid)    /*if the foreground job exist*/  
      return;  
        kill(-pid,SIGINT);     /*send signal SIGINT to the foreground process and its cild*/  
    return ;  
}  
  
/* 
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever 
 *     the user types ctrl-z at the keyboard. Catch it and suspend the 
 *     foreground job by sending it a SIGTSTP.   
 */  
void sigtstp_handler(int sig)   
{  
    pid_t pid;  
        pid=fgpid(jobs);      /*get the foreground job*/  
    if(!pid)  
      return;  
        kill(-pid,SIGTSTP);    /*send signal SIGTSTP to the foreground process and its cild*/  
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
void listjobs(struct job_t *jobs, int output_fd)   
{  
    int i;  
    char buf[MAXLINE];  
      
    for (i = 0; i < MAXJOBS; i++) {  
        memset(buf, '\0', MAXLINE);  
        if (jobs[i].pid != 0) {  
            sprintf(buf, "[%d] (%d) ", jobs[i].jid, jobs[i].pid);  
            if(write(output_fd, buf, strlen(buf)) < 0) {  
                fprintf(stderr, "Error writing to output file\n");  
                exit(1);  
            }  
            memset(buf, '\0', MAXLINE);  
            switch (jobs[i].state) {  
                case BG:   
                    sprintf(buf, "Running    ");  
                    break;  
                case FG:   
                    sprintf(buf, "Foreground ");  
                    break;  
                case ST:   
                    sprintf(buf, "Stopped    ");  
                    break;  
                default:  
                    sprintf(buf, "listjobs: Internal error: job[%d].state=%d ",   
                        i, jobs[i].state);  
            }  
            if(write(output_fd, buf, strlen(buf)) < 0) {  
                fprintf(stderr, "Error writing to output file\n");  
                exit(1);  
            }  
            memset(buf, '\0', MAXLINE);  
            sprintf(buf, "%s", jobs[i].cmdline);  
            if(write(output_fd, buf, strlen(buf)) < 0) {  
                fprintf(stderr, "Error writing to output file\n");  
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
    fprintf(stderr, "%s: %s\n", msg, strerror(errno));  
    exit(1);  
}  
  
/* 
 * app_error - application-style error routine 
 */  
void app_error(char *msg)  
{  
    fprintf(stderr, "%s\n", msg);  
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
    if (verbose)  
        printf("siquit_handler: terminating after receipt of SIGQUIT signal\n");  
    exit(1);  
}  