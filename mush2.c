#include <errno.h>
#include <fcntl.h>
#include <mush.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define READ_END 0
#define WRITE_END 1
#define NO_DUP -1
#define SHELL_PROMPT "8-P "

char isInterrupted = 0;

/*interrupted value set to true*/
void handler(int signum) 
{
    isInterrupted = 1;
}


/* cd command  */
int changeDirectory(int argc, char *argv[]) 
{
    char *path;
    struct passwd *pwd;
    int res;

    if (argc > 2) 
    {
        fprintf(stderr, "usage: cd [directory]\n");
        return -1;
    }
    /*changedir normally*/
    if (argc == 2) 
    {
        if (chdir(argv[1]) == -1) 
        {
            perror(argv[1]);
            return -1;
        }
        return 0;
    }
    /*If path not specified, change to HOME*/
    path = getenv("HOME");
    if (path != NULL && chdir(path) != -1) 
    {
        return 0;
    }
    /*Use pwuid to changedir*/
    pwd = getpwuid(getuid());
    if (pwd == NULL) 
    {
        fprintf(stderr, "unable to determine home directory\n");
        return -1;
    }
    res = chdir(pwd->pw_dir);
    if (res == -1) 
    {
        perror(pwd->pw_dir);
    }
    free(pwd);
    return res;
}


/* launches the child processes in a pipeline */
int execute(int argc, char *argv[], pipeline myPipeline) 
{
    int i;
    int res;
    int *fds;
    int in;
    int out;
    int newPipe[2];
    int numChildren;
    pid_t child;
    int status;
    sigset_t sigset;
    struct clstage *prevStage;
    struct clstage *stage;
    int p;
    int fd;

    fds = (int *)malloc(sizeof(int) * myPipeline->length * 2);
    if (fds == NULL) 
    {
        perror("malloc");
        return -1;
    }
    numChildren = 0;
    newPipe[READ_END] = -1;
    newPipe[WRITE_END] = -1;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGINT);
    prevStage = NULL;
    stage = myPipeline->stage;


    /*fill in fd table, creating pipes and opening files*/
    while (numChildren < myPipeline->length) 
    {
        in = 2 * numChildren;
        out = 2 * numChildren + 1;

        /* built-in cd command */
        if (!strcmp((stage->argv)[0], "cd")) {
            i = changeDirectory(stage->argc, stage->argv);
            if (i == -1) {
                free(fds);
                return -1;
            }
            fds[in] = -1;
            fds[out] = -1;
            numChildren++;
            prevStage = stage;
            stage = myPipeline->stage + numChildren;
            continue;
        }

        if (stage->inname != NULL) {
            fds[in] = open(stage->inname, O_RDWR);
            if (fds[in] == -1) 
            {
                fprintf(stderr, "could not open `%s`: %s\n",
                        stage->inname, strerror(errno));
                free(fds);
                return -1;
            }
        }
        
        else {
            if (prevStage == NULL) 
            {
                fds[in] = NO_DUP;
            }
            else 
            {
                fds[in] = newPipe[READ_END];
            }
        }

        
        if (stage->outname != NULL) 
        {
            p = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
            fds[out] = open(stage->outname, O_RDWR | O_CREAT | O_TRUNC, p);
            if (fds[out] == -1) 
            {
                fprintf(stderr, "could not open `%s`: %s\n",
                        stage->outname, strerror(errno));
                free(fds);
                return -1;
            }
        }
        
        else {
            if (numChildren + 1 == myPipeline->length) 
            {
                fds[out] = NO_DUP;
            }
            else 
            {
                pipe(newPipe);
                fds[out] = newPipe[WRITE_END];
            }
        }

        numChildren++;
        prevStage = stage;
        stage = myPipeline->stage + numChildren;
    }

    /* launch children */
    for (i = 0; i < myPipeline->length; i++) 
    {
        stage = myPipeline->stage + i;

        if (!strcmp(stage->argv[0], "cd")) 
        {
            numChildren--;
            continue;
        }

        in = 2 * i;
        out = 2 * i + 1;

        child = fork();
        if (child == -1) 
        {
            fprintf(stderr, "fork `%s`: %s\n", argv[0], strerror(errno));
            return -1;
        }

        /* child */
        if (child == 0) 
        {
            /* I/O redirection */
            if (fds[in] != NO_DUP) 
            {
                dup2(fds[in], STDIN_FILENO);
            }
            if (fds[out] != NO_DUP) 
            {
                dup2(fds[out], STDOUT_FILENO);
            }

            /* clean up duplicate FDs */
            for (i = 0; i < myPipeline->length * 2; i++) 
            {
                close(fds[i]);
            }

            /* unblock interrupts and exec child process */
            sigprocmask(SIG_UNBLOCK, &sigset, 0);
            execvp(stage->argv[0], stage->argv);

            /* _exit from child if exec failed */
            perror((stage->argv)[0]);
            _exit(EXIT_FAILURE);
        }
    }

    /* close write ends of children*/
    for (i = 1; i < myPipeline->length * 2; i += 2) 
    {
        close(fds[i]);
    }

    /* wait for all the children */
    i = 0;
    sigprocmask(SIG_UNBLOCK, &sigset, 0);
    while (i < numChildren) 
    {
        res = wait(&status);
        if (res == -1) {
            /* if (errno == EINTR) 
            {
                i++;
            } */
            i++;
        }
    }

    free(fds);
    return 0;
}

void printLine(char prompt) 
{
    if (prompt == 1) 
    {
        printf("\n");
        fflush(stdout);
    }
}

int main(int argc, char *argv[]) 
{
    FILE *inputFile;
    char *line;
    pipeline myPipeline;
    sigset_t signalSet;
    struct sigaction signalAction;
    char prompt = 0;

    /* arg things */
    if (argc == 1) 
    {
        inputFile = stdin;
        prompt = 1;
    }
    else if (argc == 2) 
    {
        inputFile = fopen(argv[1], "r");
        if (inputFile == NULL) {
            perror(argv[1]);
            exit(EXIT_FAILURE);
        }
    }
    else 
    {
        fprintf(stderr, "usage: mush2 [infile]\n");
        exit(EXIT_FAILURE);
    }

    if (prompt && isatty(STDIN_FILENO) && isatty(STDOUT_FILENO)) 
    {
        prompt = 1;
    }

    sigemptyset(&signalSet);
    sigaddset(&signalSet, SIGINT);
    /* SIGINT handler */
    memset(&signalAction, 0, sizeof(signalAction));
    signalAction.sa_handler = handler;
    sigaction(SIGINT, &signalAction, NULL);

    while (!feof(inputFile)) 
    {
        if (prompt) 
        {
            printf("%s", SHELL_PROMPT);
            fflush(stdout);
        }

        /* read into pipeline */
        line = readLongString(inputFile);
        if (line == NULL) 
        {
            printLine(prompt);
            interrupted = 0;
            continue;
        }
        myPipeline = crack_pipeline(line);
        free(line);
        if (myPipeline == NULL) 
        {
            interrupted = 0;
            continue;
        }
        yylex_destroy();
        

        /*If interrupt, free pipeline*/
        if (interrupted) 
        {
            free_pipeline(myPipeline);
            printLine(prompt);
            interrupted = 0;
            continue;
        }

        /* block interrupts while launching children */
        sigprocmask(SIG_BLOCK, &signalSet, 0);

        /* execute the processes */
        execute(argc, argv, myPipeline);
        if (interrupted) 
        {
            printLine(prompt);
            interrupted = 0;
        }

        free_pipeline(myPipeline);
        fflush(stdout);
    }
    /* cleanup */
    fclose(inputFile);
    return 0;
}

