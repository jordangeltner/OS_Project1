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
#include <pwd.h>

/************Private include**********************************************/
#include "runtime.h"
#include "io.h"
#include "interpreter.h"

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
  int status;
  int jid;
} bgjobL;

typedef struct alias_l {
  char* name;
  char* cmdline;
  struct alias_l* next;
} aliasL;

/* the pids of the background processes */
bgjobL *bgjobs = NULL;

/* alias list */
aliasL *aliases = NULL;

// the current foreground process group id
pid_t g_fgpgid = 1;

// flag that indicates whether we should continue waiting on a foreground process or not.
bool g_waitfg = TRUE;


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
/* returns whether a string is an alias */
static bool IsAlias(char* name);
/* runs an aliased command */
static void aliascmd(commandT* cmd);
/* adds an alias to the global list */
static void addalias(commandT* cmd);
/* substitues an alias for its cmdline */
static char* subalias(char* name);
/* removes an alias from the global list */
static void unalias(char* name);
/* checks what status of child process means and prints message */
static int ChildStatus(int status,pid_t pid);
/* print out table of jobs */
static void jobscall();
/* adds a job to the job list */
static void addjob(commandT* cmd, pid_t childId);
/* finds the lowest available jid */
static pid_t getLowJid();
/* foreground the given job or the next one */
static void fgcall(commandT* cmd);
/* gets our fgpgid for tsh.c signal handler*/
pid_t getfgpgid();
/* busy loop that waits on a foreground process without calling waitpid */
static void waitfg();
/*sets global g_waitfg to false, used by sig handler to indicate that a fg job is finished without reaping it */
void set_waitfg();
/************External Declaration******************************************/

/**************Implementation***********************************************/


pid_t getfgpgid(){
  return g_fgpgid;
}
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
  else if (strcmp(cmd->argv[0],"alias")==0){
    addalias(cmd);
  }
  else if (strcmp(cmd->argv[0],"unalias")==0){
    unalias(cmd->argv[1]);
  }
  else if (IsAlias(cmd->argv[0])){
    aliascmd(cmd);
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
    //block SIGCHLD signals so that we will not receive one until the job has been executed
    sigset_t blocked;
    sigemptyset (&blocked);
    sigaddset(&blocked,SIGCHLD);
    sigprocmask(SIG_SETMASK,&blocked,NULL);
    sigset_t childset;
    sigemptyset (&childset);
    sigaddset(&childset,SIGCHLD);
    pid_t childId = fork();
    //int status;
    

    //fork failed
    if (childId < 0)
    {
       printf("Couldn't fork.\n");
       fflush(stdout);
       return;
    }

    //we're in the child
    else if (childId == 0)
    {
      //child job, so set unique pgid
      setpgid(0,0);
      //unblock SIGCHLD signals, so that it can detect them from the exec
      sigprocmask(SIG_UNBLOCK,&childset,NULL);
//       if (cmd->bg != 0){
// 	setpgid(0,0);
//       }
//       //foreground job, so set pgid to -ppid
//       else{
// 	printf("CHILD: ppid:%d  pid:%d  pgid:%d\n",getppid(),getpid(),getpgid(getpid()));
// 	fflush(stdout);
//       }
      //printf("Running command %s in the fork.\n", cmd->name);
      if (-1 == execv(cmd->name, cmd->argv))
        _exit(EXIT_FAILURE);
      _exit(EXIT_SUCCESS);
    }
    //parent's execution
    else
    {
      //sigprocmask(SIG_UNBLOCK,&childset,NULL);
      //printf("Parent of: %d\n", childId);
      if (cmd->bg == 0)
      {
	printf("fg:%s pid:%d ppid:%d\n",cmd->cmdline,childId,getpid());
	fflush(stdout);
// 	printf("PARENT: ppid:%d  pid:%d  pgid:%d\n",getppid(),getpid(),getpgid(getpid()));
// 	fflush(stdout);
	//unblock SIGCHLD signals, because waiting on a fg job_id
	sigprocmask(SIG_UNBLOCK,&childset,NULL);
	g_fgpgid = childId;
	
	waitfg();
	printf("passed waitfg loop for fg:%s\n",cmd->cmdline);
	fflush(stdout);
	g_waitfg = TRUE;
	
        //waitpid(childId, &status, 0);
      }

  //background job, so add to job list
      else
      {
	addjob(cmd, childId);
	//job has been added, so unblock SIGCHLD
	sigprocmask(SIG_UNBLOCK,&childset,NULL);
      }
    }
  }
  else
  {
    execv(cmd->name, cmd->argv);
  }
  return; 
}

static void addjob(commandT* cmd, pid_t childId)
{
  printf("bg:%s pid:%d ppid:%d\n",cmd->cmdline,childId,getpid());
  fflush(stdout);
  bgjobL *job = bgjobs;
  bgjobL *prev = NULL;

  //add new job to end of job list
  while(job != NULL){
    prev = job;
    job = job->next;
  }

  //allocate new space for the new job
  job = malloc(sizeof(bgjobL));
  job->pid = childId;
  job->next = NULL;
  job->cmd = CreateCmdT(cmd->argc);
  job->cmd->cmdline = cmd->cmdline;
  job->status = -1;
  job->jid = getLowJid();

  //if this is the first job
  if (bgjobs == NULL)
    bgjobs = job;
  else
    prev->next = job;
}

static pid_t getLowJid()
{
  //min jid is 1, check each job very slowly. O(n!)
  bgjobL* job = bgjobs;
  pid_t result = 1;

  //iterate through copy while removing matches (SUPER DUMB)
  while (job != NULL){
    if (result == job->jid){
      job = bgjobs;
      result++;
    }
    else
      job = job->next;
  }
  return result;
}

int edit_bgjob_status(pid_t pid, int status){
  bgjobL *job = bgjobs;
  while(job!=NULL){
    if(job->pid==pid){
      job->status = status;
      return 0;
    }
  }
  return -1;
}

static void waitfg(){
  while (g_waitfg){
    sleep(1);
  }
  return;
}

void set_waitfg(){
  g_waitfg = FALSE;
  return;
}

static bool IsBuiltIn(commandT* cmd)
{
  //if the cmd is bg, fg, jobs, or cd it's built in so return True.
  return (strcmp(cmd->argv[0],"bg")==0) || (strcmp(cmd->argv[0],"jobs")==0) ||(strcmp(cmd->argv[0],"fg")==0) ||
         (strcmp(cmd->argv[0],"cd")==0);
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
    fgcall(cmd);
  }
  else if(strcmp(cmd->argv[0],"cd")==0){
    if (cmd->argc >= 2){
      if(-1 == chdir(cmd->argv[1])){
        printf("Failed to change to %s\n",cmd->argv[1]);
	fflush(stdout);
      }
    }
  }
  return;
}

static char *subalias(char* name)
{
  aliasL* a = aliases;
  while (a != NULL){
    if (strcmp(name, a->name) == 0){
      return a->cmdline;
    }
    a = a->next;
  }
  return NULL;
}

static void addalias(commandT* cmd)
{
  //print out aliases
  if (cmd->argc == 1){
    aliasL* a = aliases;
    while (a != NULL){
      printf("%s: %s\n", a->name, a->cmdline);
      a = a->next;
    }
    return;
  }
  //make a new alias struct
  aliasL* a = malloc(sizeof(aliasL));
  char * name = NULL;

  int argc = 0;
  char * cmdline = NULL;
  char *token;
  
  //get the first space (alias< >name="value")
  const char s1[2] = " ";
  //get the name when argc==1
  const char s2[4] = "=\"'";

  token = strtok(cmd->cmdline, s1);
  while (token != NULL){
    token = strtok(NULL, s2);
    argc++;
    //we got the second token, name of alias
    if (argc == 1){
      name = token;
    }
    //we got the third token, value of alias
    else if (argc == 2){
      cmdline = token;
    }
  }
  a->cmdline = cmdline;
  a->name = name;

  //add the first alias
  if (aliases == NULL){
    aliases = a;
    a->next = NULL;
  }
  //add alias to the front of the list
  else{
    a->next = aliases;
    aliases = a;
  }
}

static void unalias(char* name)
{
  aliasL* a = aliases;
  aliasL* prev = NULL;
  while(a != NULL){
    //found the matching alias
    if (strcmp(a->name, name) == 0){
      //removing the head
      if (a == aliases){
        aliases = a->next;
      }
      //removing another
      else{
        prev->next = a->next;
      }
      free(a);
      return;
    }
    prev = a;
    a = a->next;
  }
  printf("unalias: %s: not found\n", name);
}

static void aliascmd(commandT* cmd)
{
  aliasL* a = aliases;
  while(a != NULL){
    if (strcmp(a->name, cmd->argv[0])==0){
      //get the case where second arg is alias
      if (cmd->argc == 2){
        if (IsAlias(cmd->argv[1])){
          char * combcmd = subalias(cmd->argv[0]);
          //special case for piazza post about shell not finding ~/
          if (strcmp(subalias(cmd->argv[1]), "~/") == 0){
            strcat(combcmd, " ");
            //gets the home directory of the current user
            strcat(combcmd, getpwuid(getuid())->pw_dir);
            Interpret(combcmd);
            return;
          }
          strcat(combcmd, " ");
          strcat(combcmd, subalias(cmd->argv[1]));
          Interpret(combcmd);
          return;
        }
      }
      Interpret(a->cmdline);
      return;
    }
    a = a->next;
  }
  printf("%s: command not found\n",cmd->argv[0]);

  return;
}

static bool IsAlias(char* name)
{
  aliasL* a = aliases;
  if (a == NULL){
  //  printf("No aliases\n");
    return FALSE;
  }
  while (a != NULL){
   // printf("Comparing [%s] to [%s]\n", a->name, name);
    if (strcmp(a->name,name) == 0){
      return TRUE;
    }
    a = a->next;
  }
  return FALSE;
}

void CheckJobs()
{
  bgjobL *job = bgjobs;
  bgjobL *old_job = bgjobs;
  bgjobL *prev_job = NULL;
  int k = 0;
  char* state;
  char* cmdstring;
  int childstatus;
  while(job != NULL){
    k++;
    cmdstring = strdup(job->cmd->cmdline);
    childstatus = ChildStatus(job->status,job->pid);
    if (childstatus==-1){
      //child running
      state = strdup("Running                 ");
    }
    else if(childstatus==0) {
      //child process exited
      state = strdup("Done                    ");
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
      printf("[%d]   %s %s\n", k, state, cmdstring);
      fflush(stdout);
      continue;
    }
    else{
      //child stopped
      state = strdup("Stopped                 ");
       printf("[%d]   %s %s\n", k,state, cmdstring);
       fflush(stdout);
    }
    prev_job = job;
    job = job->next;
  }
  
}

static void fgcall(commandT* cmd){
  bgjobL *job = bgjobs;
  int k = 0;
  pid_t target = -1;
  //no argument given, foreground default value from joblist if there is one
  if (cmd->argc == 1){
    target = job->pid;
  }
  //match either job_id or job number to joblist
  else{
    pid_t match_id = atoi(cmd->argv[1]);
    while(job != NULL){
      k++;
      if (match_id == k || match_id == job->pid){
        target = job->pid;
        break;
      }
      job = job->next;
    }
  }
  if (target > 0){
    int childstatus = ChildStatus(job->status,target);
    if(childstatus==0){
      //done
      printf("[1] Done                     %s\n", job->cmd->cmdline);
      fflush(stdout);
    }
    else if(childstatus ==-1) {
      //stopped
      printf("Stopped                  %s\n", job->cmd->cmdline);
      fflush(stdout);
    }
    else{
      //running
      g_fgpgid = target;
      waitfg();
      g_waitfg = TRUE;
    }
  }
  else{
    if (cmd->argc == 1){
      printf("fg: current: no such job\n");
      fflush(stdout);
    }
    else{
      printf("fg: %s: no such job\n",cmd->argv[1]);
      fflush(stdout);
    }
  }
}

static void jobscall()
{
  bgjobL *job = bgjobs;
//   char current;
  while(job != NULL){
    char* state;
//     if(k==1){
//       current = '+';
//     }
//     else if(k==2){
//       current = '-';
//     }
//     else{
//       current = ' ';
//     }
    //test cases don't have this..
//     current = ' ';
    int childstatus = ChildStatus(job->status,job->pid);
    //check the status of child process
    if (childstatus ==-1){
      //running
      state = strdup("Running                 ");
    }
    else if (childstatus==0){
      //child running
      state = strdup("Done                    ");
    }
    else {
      //child stopped
      state = strdup("Stopped                 ");
    }
    // add command line to job list
    printf("[%d]   %s %s &\n", job->jid, state, job->cmd->cmdline);
    fflush(stdout);
    job = job->next;
  }
  
}


//informs CheckJobs what the status of the child process means
// i.e. is it terminated? how? need to reap?
// return 0 if program exited, 1 if stopped, -1 if running
int ChildStatus(int status,pid_t pid)
{
  //still running
 if (status==-1){
   return -1;
 }
 if (WIFEXITED(status))
   return 0;
 else if (WIFSIGNALED(status)){
   printf("Process %d received a signal.\n",pid);
   fflush(stdout);
 }
 else if (WIFSTOPPED(status)){
   printf("Process %d was stopped while executing.\n",pid);
   fflush(stdout);
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
