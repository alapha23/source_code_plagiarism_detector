void eval(char *cmdline)   
{  
    char *argv[MAXARGS];  
    char buf[MAXLINE];  
    int bg;  
    pid_t pid;  
    sigset_t mask;  
  
    strcpy(buf, cmdline);  
    bg = parseline(buf, argv);  
    if(argv[0] == NULL)  
         return;  
   /*防止addjob和deletejob冲突，先阻塞SIGCHLD信号*/  
    sigemptyset(&mask);  
    sigaddset(&mask, SIGCHLD);  
    sigprocmask(SIG_BLOCK, &mask, NULL);  
  
    if(!builtin_cmd(argv)){  
        if((pid = fork()) == 0){  
            sigprocmask(SIG_UNBLOCK, &mask, NULL);  
            if(setpgid(0, 0) < 0)  
                unix_error("eval: setgpid failed.\n");  
            if(execve(argv[0], argv, environ) < 0 ){  
                printf("%s: Command not found.\n", argv[0]);  
                exit(0);  
            }  
        }  
        /*前台程序*/  
        if(!bg)  
            addjob(jobs, pid, FG, cmdline);  
        else  
            addjob(jobs, pid, BG, cmdline);  
        sigprocmask(SIG_UNBLOCK, &mask, NULL);  
        /*若在前台运行，阻塞到pid所代表到作业状态state不是FG*/  
        if(!bg)  
            waitfg(pid);  
        else  
            printf("[%d] (%d) %s\n", pid2jid(pid), pid, cmdline);  
    }  
    return;  
}  

int builtin_cmd(char **argv)   
{  
    if(!strcmp(argv[0], "quit"))  
        exit(0);  
    if(!strcmp(argv[0], "jobs")){  
        listjobs(jobs);  
        return 1;  
    }  
    /*后面到作业控制会用到*/  
    if(!strcmp(argv[0], "bg") || !strcmp(argv[0], "fg")){  
        do_bgfg(argv);  
        return 1;  
    }  
    return 0;     /* not a builtin command */  
}  

void sigint_handler(int sig)   
{  
    pid_t pid = fgpid(jobs);  
    int jid = pid2jid(pid);  
    if(pid != 0){  
        printf("Job [%d] terminated by SIGINT.\n", jid);  
        deletejob(jobs, pid);  
        kill(-pid, sig);  
    }  
    return;  
}  

void sigtstp_handler(int sig)   
{  
    pid_t  pid = fgpid(jobs);  
    int jid = pid2jid(pid);  
  
    if(pid != 0){  
        printf("Job[%d] stopped by SIGTSTP", jid);  
        (*getjobpid(jobs, pid)).state = ST;  
        kill(-pid, sig);  
    }  
    return;  
}  

void sigchld_handler(int sig)   
{  
    pid_t pid;  
    int status, child_sig;  
    while((pid = waitpid(-1, &status, WUNTRACED | WNOHANG)) > 0 ){  
        printf("Handling chlid proess %d\n", (int)pid);  
        /*handle SIGTSTP*/  
        if( WIFSTOPPED(status) )  
            sigtstp_handler( WSTOPSIG(status) );  
        /*handle child process interrupt by uncatched signal*/  
        else if( WIFSIGNALED(status) ) {  
            child_sig = WTERMSIG(status);  
            if(child_sig == SIGINT)  
                sigint_handler(child_sig);  
        }  
        else      
            deletejob(jobs, pid);  
    }  
    return;  
}  

void do_bgfg(char **argv)   
{  
    char *id = argv[1];  
    struct job_t *job;  
  
    int jobid;  
    if(id[0] == '%')  
        jobid = atoi(id+1);  
    if((job = getjobjid(jobs, jobid)) == NULL){  
        printf("Job is not exist.\n");  
        return;  
    }  
    if(!strcmp(argv[0], "bg")){  
        job->state = BG;  
        kill(-1* job->pid, SIGCONT);  
    }  
    if(!strcmp(argv[0], "fg")){  
        job->state = FG;  
        kill(-1 * job->pid, SIGCONT);  
        waitfg(job->pid);  
    }  
    return;  
}  
<span style="font-size:14px;">下面是waitfg：</span><pre name="code" class="cpp">void waitfg(pid_t pid)  
{  
    while(pid == fgpid(jobs));  
    return;  
}  
