/***************************************************************************
 *  Title: Runtime environment 
 * -------------------------------------------------------------------------
 *    Purpose: Runs commands
 *    Author: Stefan Birrer
 *    Version: $Revision: 1.1 $
 *    Last Modification: $Date: 2005/10/13 05:24:59 $
 *    File: $RCSfile: runtime.c,v $
 *    Copyright: (C) 2002 by Stefan Birrer
 ***************************************************************************/
/***************************************************************************
 *  ChangeLog:
 * -------------------------------------------------------------------------
 *    $Log: runtime.c,v $
 *    Revision 1.1  2005/10/13 05:24:59  sbirrer
 *    - added the skeleton files
 *
 *    Revision 1.6  2002/10/24 21:32:47  sempi
 *    final release
 *
 *    Revision 1.5  2002/10/23 21:54:27  sempi
 *    beta release
 *
 *    Revision 1.4  2002/10/21 04:49:35  sempi
 *    minor correction
 *
 *    Revision 1.3  2002/10/21 04:47:05  sempi
 *    Milestone 2 beta
 *
 *    Revision 1.2  2002/10/15 20:37:26  sempi
 *    Comments updated
 *
 *    Revision 1.1  2002/10/15 20:20:56  sempi
 *    Milestone 1
 *
 ***************************************************************************/
#define __RUNTIME_IMPL__

/************System include***********************************************/
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <dirent.h>

/************Private include**********************************************/
#include "runtime.h"
#include "io.h"

/************Defines and Typedefs*****************************************/
/*  #defines and typedefs should have their names in all caps.
 *  Global variables begin with g. Global constants with k. Local
 *  variables should be in all lower case. When initializing
 *  structures and arrays, line everything up in neat columns.
 */

/************Global Variables*********************************************/

#define NBUILTINCOMMANDS (sizeof BuiltInCommands / sizeof(char*))

typedef struct bgjob_l {
  pid_t pid;
  struct bgjob_l* next;
  commandT* cmd;
} bgjobL;

/* the pids of the background processes */
bgjobL *bgjobs = NULL;

/************Function Prototypes******************************************/
/* run command */
static void RunCmdFork(commandT*, bool);
/* runs an external program command after some checks */
static void RunExternalCmd(commandT*, bool);
/* resolves the path and checks for exutable flag */
static bool ResolveExternalCmd(commandT*);
/* forks and runs a external program */
static void Exec(commandT*, bool);
/* runs a builtin command */
static void RunBuiltInCmd(commandT*);
/* checks whether a command is a builtin command */
static bool IsBuiltIn(commandT*);
/* checks whether a file is in the directory */
static bool fileInDir(char*, char* the_dir);
/* checks what status of child process means and prints message */
static int ChildStatus(int status,pid_t pid);
/* checks the length of the backgrounded jobs list */
static int joblist_length();
/* print out table of jobs */
static void jobscall();
/* sets bg field of cmd based on presence of & at end of argv */
//static void set_bg(commandT* cmd);
/************External Declaration*****************************************/

/**************Implementation***********************************************/
int total_task;
void RunCmd(commandT** cmd, int n)
{
  int i;
  total_task = n;
  if(n == 1)
    RunCmdFork(cmd[0], TRUE);
  else{
    RunCmdPipe(cmd[0], cmd[1]);
    for(i = 0; i < n; i++)
      ReleaseCmdT(&cmd[i]);
  }
}


//prints cmd args 1 and 2
//  printf("arg1:%s   arg2:%s",cmd[0]->argv[0],cmd[0]->argv[1]);
void RunCmdFork(commandT* cmd, bool fork)
{
  if (cmd->argc<=0)
    return;
  if (IsBuiltIn(cmd))
  {
 //   printf("Hey it's builtin: %s\n", cmd->argv[0]);
    RunBuiltInCmd(cmd);
  }
  else
  {
    RunExternalCmd(cmd, fork);
  }
}

void RunCmdBg(commandT* cmd)
{
  // TODO
}

void RunCmdPipe(commandT* cmd1, commandT* cmd2)
{
}

void RunCmdRedirOut(commandT* cmd, char* file)
{
}

void RunCmdRedirIn(commandT* cmd, char* file)
{
}


/*Try to run an external command*/
static void RunExternalCmd(commandT* cmd, bool fork)
{
  if (ResolveExternalCmd(cmd)){
    Exec(cmd, fork);
  }
  else {
    printf("%s: command not found\n", cmd->argv[0]);
    fflush(stdout);
    ReleaseCmdT(&cmd);
  }
}

/*Find the executable based on search list provided by environment variable PATH*/
static bool ResolveExternalCmd(commandT* cmd)
{
  char *pathlist, *c;
  char buf[1024];
  int i, j;
  struct stat fs;

  if(strchr(cmd->argv[0],'/') != NULL){
    if(stat(cmd->argv[0], &fs) >= 0){
      if(S_ISDIR(fs.st_mode) == 0)
        if(access(cmd->argv[0],X_OK) == 0){/*Whether it's an executable or the user has required permisson to run it*/
          cmd->name = strdup(cmd->argv[0]);
          return TRUE;
        }
    }
    return FALSE;
  }
  pathlist = getenv("PATH");
  if(pathlist == NULL) return FALSE;
  i = 0;
  while(i<strlen(pathlist)){
    c = strchr(&(pathlist[i]),':');
    if(c != NULL){
      for(j = 0; c != &(pathlist[i]); i++, j++)
        buf[j] = pathlist[i];
      i++;
    }
    else{
      for(j = 0; i < strlen(pathlist); i++, j++)
        buf[j] = pathlist[i];
    }
    buf[j] = '\0';
    strcat(buf, "/");
    strcat(buf,cmd->argv[0]);
    if(stat(buf, &fs) >= 0){
      if(S_ISDIR(fs.st_mode) == 0)
        if(access(buf,X_OK) == 0){/*Whether it's an executable or the user has required permisson to run it*/
          cmd->name = strdup(buf); 
          return TRUE;
        }
    }
  }
  return FALSE; /*The command is not found or the user don't have enough priority to run.*/
}

static void Exec(commandT* cmd, bool forceFork)
{
  //first check if command is background command and set flag
  //set_bg(cmd);
  //printf("background flag set to %d\n",cmd->bg);
  if (forceFork)
  {
    pid_t childId = fork();
    int status;
    

    //fork failed
    if (childId < 0)
    {
       printf("Couldn't fork.\n");
       return;
    }

    //we're in the child
    else if (childId == 0)
    {
      //printf("Running command %s in the fork.\n", cmd->name);
      if (-1 == execvp(cmd->name, cmd->argv))
        exit(EXIT_FAILURE);
      exit(EXIT_SUCCESS);
    }
    //parent's execution
    else
    {
      //printf("Parent of: %d\n", childId);
      if (cmd->bg == 0)
      {
        waitpid(childId, &status, 0);
      }

  //background job, so add to job list
      else
      {
	bgjobL *job = malloc(sizeof(bgjobL));
	job->pid = childId;
	job->next = bgjobs;
        job->cmd = CreateCmdT(cmd->argc);
        job->cmd->cmdline = cmd->cmdline;
	bgjobs = job;
        //test printouts don't print this.
	//printf("[%d] %d\n",joblist_length(),childId);
      }
    }
  }
  else
  {
    execvp(cmd->argv[0], cmd->argv);
  }
  return; 
}

static int joblist_length(){
  int k=0;
  bgjobL *localbgjobs = bgjobs;
  while(localbgjobs!=NULL){
    k++;
    localbgjobs = localbgjobs->next;
  }
  return k;
}

static bool IsBuiltIn(commandT* cmd)
{
  //if the cmd is bg, fg, jobs, or cd it's built in so return True.
  return (strcmp(cmd->argv[0],"bg")==0) || (strcmp(cmd->argv[0],"jobs")==0) ||(strcmp(cmd->argv[0],"fg")==0) ||
         (strcmp(cmd->argv[0],"cd")==0);
}


//fileInDir was sourced from the following page.
//http://stackoverflow.com/questions/612097/how-can-i-get-a-list-of-files-in-a-directory-using-c-or-c
static bool fileInDir(char* cmd, char* the_dir)
{
  //search through /bin for cmd and return true if its a member
  DIR *dir;
  struct dirent *ent; 
  if ((dir = opendir (the_dir)) != NULL)
  {
    while ((ent = readdir (dir)) != NULL)
      if (strcmp(cmd, ent->d_name) == 0)
      {
        return TRUE;
      }
  }
  return FALSE;     
}


static void RunBuiltInCmd(commandT* cmd)
{
  cmd->name = cmd->argv[0];
  //hardcoded commands
  //bg
  if((strcmp(cmd->argv[0],"bg")==0) && (bgjobs!=NULL)){
    //send signal to most recent bg job (head of job list)
    if(cmd->argv[1]==NULL){
      kill(bgjobs->pid,SIGCONT);
    }
    //send signal to specified pid
    else{
      pid_t localpid = atoi(cmd->argv[1]);
      kill(localpid,SIGCONT);
    }
  }
  else if(strcmp(cmd->argv[0],"jobs")==0){
    jobscall();
  }
  else if(strcmp(cmd->argv[0],"fg")==0){
  }
  else if(strcmp(cmd->argv[0],"cd")==0){
  }
  return;
}

void CheckJobs()
{
  bgjobL *job = bgjobs;
  bgjobL *old_job = bgjobs;
  bgjobL *prev_job = NULL;
  int status;
  pid_t idOut;
  int k = 0;
  while(job != NULL){
    k++;
    idOut = waitpid(job->pid,&status,WNOHANG | WUNTRACED | WCONTINUED);
    //check the status of child process
    if (idOut ==-1){
      //error in waitpid call
      printf("waitpid failed to return on id %d",job->pid);
      exit(EXIT_FAILURE);
    }
    else if (idOut==0){
      //child running
    }
     //child not running, find out why (stopped or exited) then remove from job list
    else {
      //child process exited
      if(ChildStatus(status,job->pid)==0){
         printf("[%d] Done %s\n", k, job->cmd->cmdline);
	if(prev_job == NULL){
          // free up memory used by old job, move pointer to next
	  bgjobs = job->next;
          old_job = job;
          ReleaseCmdT(&old_job->cmd);
          free(old_job);
	  job = bgjobs;
	}
	else{
	  prev_job->next = job->next;
          old_job = job;
          ReleaseCmdT(&old_job->cmd);
          free(old_job);
	  job = prev_job->next;
	}
	continue;
      }
      //child process stopped
      else{
      }
    }
    prev_job = job;
    job = job->next;
  }
  
}

static void jobscall()
{
  bgjobL *job = bgjobs;
  int status;
  pid_t idOut;
  int k =0;
  char current;
  while(job != NULL){
    char* state;
    k++;
    if(k==1){
      current = '+';
    }
    else if(k==2){
      current = '-';
    }
    else{
      current = ' ';
    }
    idOut = waitpid(job->pid,&status,WNOHANG | WUNTRACED | WCONTINUED);
    //check the status of child process
    if (idOut ==-1){
      //error in waitpid call
      printf("waitpid failed to return on id %d",job->pid);
      exit(EXIT_FAILURE);
    }
    else if (idOut==0){
      //child running
      state = strdup("Running");
    }
     //child not running, find out why (stopped or exited) then remove from job list
    else {
      //child process exited
      if(ChildStatus(status,job->pid)==0){
	state = strdup("Done");
        printf("[%d] %c %s %s\n", k, current, state, job->cmd->cmdline);
      }
      //child process stopped
      else{
	state = strdup("Stopped");
        printf("[%d] %c %s %s\n", k, current, state, job->cmd->cmdline);
      }
    }
    // add command line to job list
    printf("[%d] %c %s %s\n", k, current, state, job->cmd->cmdline);
    job = job->next;
  }
  
}


//informs CheckJobs what the status of the child process means
// i.e. is it terminated? how? need to reap?
// return 0 if program exited, 1 if stopped
int ChildStatus(int status,pid_t pid)
{
 if (WIFEXITED(status))
   return 0;
 else if (WIFSIGNALED(status))
   printf("Process %d received a signal.\n",pid);
 else if (WIFSTOPPED(status)){
   printf("Process %d was stopped while executing.\n",pid);
   return 1;
 }
 return 0;
}

commandT* CreateCmdT(int n)
{
  int i;
  commandT * cd = malloc(sizeof(commandT) + sizeof(char *) * (n + 1));
  cd -> name = NULL;
  cd -> cmdline = NULL;
  cd -> is_redirect_in = cd -> is_redirect_out = 0;
  cd -> redirect_in = cd -> redirect_out = NULL;
  cd -> argc = n;
  for(i = 0; i <=n; i++)
    cd -> argv[i] = NULL;
  return cd;
}

/*Release and collect the space of a commandT struct*/
void ReleaseCmdT(commandT **cmd){
  int i;
  if((*cmd)->name != NULL) free((*cmd)->name);
  if((*cmd)->cmdline != NULL) free((*cmd)->cmdline);
  if((*cmd)->redirect_in != NULL) free((*cmd)->redirect_in);
  if((*cmd)->redirect_out != NULL) free((*cmd)->redirect_out);
  for(i = 0; i < (*cmd)->argc; i++)
    if((*cmd)->argv[i] != NULL) free((*cmd)->argv[i]);
  free(*cmd);
}
