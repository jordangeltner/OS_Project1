/***************************************************************************
 *  Title: MySimpleShell 
 * -------------------------------------------------------------------------
 *    Purpose: A simple shell implementation 
 *    Author: Stefan Birrer
 *    Version: $Revision: 1.1 $
 *    Last Modification: $Date: 2005/10/13 05:24:59 $
 *    File: $RCSfile: tsh.c,v $
 *    Copyright: (C) 2002 by Stefan Birrer
 ***************************************************************************/
/***************************************************************************
 *  ChangeLog:
 * -------------------------------------------------------------------------
 *    $Log: tsh.c,v $
 *    Revision 1.1  2005/10/13 05:24:59  sbirrer
 *    - added the skeleton files
 *
 *    Revision 1.4  2002/10/24 21:32:47  sempi
 *    final release
 *
 *    Revision 1.3  2002/10/23 21:54:27  sempi
 *    beta release
 *
 *    Revision 1.2  2002/10/15 20:37:26  sempi
 *    Comments updated
 *
 *    Revision 1.1  2002/10/15 20:20:56  sempi
 *    Milestone 1
 *
 ***************************************************************************/
#define __MYSS_IMPL__

/************System include***********************************************/
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <unistd.h>

/************Private include**********************************************/
#include "tsh.h"
#include "io.h"
#include "interpreter.h"
#include "runtime.h"

/************Defines and Typedefs*****************************************/
/*  #defines and typedefs should have their names in all caps.
 *  Global variables begin with g. Global constants with k. Local
 *  variables should be in all lower case. When initializing
 *  structures and arrays, line everything up in neat columns.
 */

#define BUFSIZE 80

/************Global Variables*************************************/
sigset_t g_blocked;
/************Function Prototypes******************************************/
/* handles SIGINT and SIGSTOP signals */	
static void sig(int);

/************External Declaration*****************************************/

/**************Implementation***********************************************/

int main (int argc, char *argv[])
{
  /* Initialize command buffer */
  char* cmdLine = malloc(sizeof(char*)*BUFSIZE);
  /* shell initialization */
  if (signal(SIGINT, sig) == SIG_ERR) PrintPError("SIGINT");
  if (signal(SIGTSTP, sig) == SIG_ERR) PrintPError("SIGTSTP");
  if (signal(SIGCONT,sig)==SIG_ERR) PrintPError("SIGCONT");
  if (signal(SIGCHLD, sig) == SIG_ERR) PrintPError("SIGCHLD");
  sigemptyset (&g_blocked);
  sigaddset(&g_blocked,SIGCHLD);
  sigaddset(&g_blocked,SIGTSTP);
  sigaddset(&g_blocked,SIGINT);
  sigaddset(&g_blocked,SIGCONT);

  while (!forceExit) /* repeat forever */
  {

    //printf("tsh> ");
    /* read command line */
    getCommandLine(&cmdLine, BUFSIZE);

    if(strcmp(cmdLine, "exit") == 0)
    {
      forceExit=TRUE;
      continue;
    }

    /* checks the status of background jobs */
    CheckJobs();

    /* interpret command and line
     * includes executing of commands */
    Interpret(cmdLine);

  }

  /* shell termination */
  commandT* holdcmd = getfgcmd();
  ReleaseCmdT(&holdcmd);
  free(cmdLine);
  return 0;
} /* end main */

static void sig(int signo)
{
  sigprocmask(SIG_SETMASK,&g_blocked,NULL);
  int status;
  if(signo==SIGCHLD){
    pid_t pid = waitpid(-1,&status,WNOHANG | WUNTRACED | WCONTINUED);
    while(pid>0){
      if(getfgpgid()==pid){
	//foreground, so release the busy loop
	set_waitfg();
      }
      else{
	//background
	edit_bgjob_status(pid,status);
      }
      pid = waitpid(-1,&status,WNOHANG | WUNTRACED | WCONTINUED);
    }
    sigprocmask(SIG_UNBLOCK,&g_blocked,NULL);
    return;
  }
  //are you the shell? Send signal on to fg child
  if(getfgpgid()!=1 && getfgpgid()!=getpid() && getfgpgid()!=getppid()){
    int jid=0;
    switch (signo){
      case SIGTSTP:
// 	printf("about to addjob cmd:%s  pid:%d\n",getfgcmd()->cmdline,getfgpgid());
// 	fflush(stdout);
	jid = addjob(getfgcmd(),getfgpgid());
	break;
    }
    kill(getfgpgid(),signo);
    if(signo==SIGTSTP){
      waitpid(getfgpgid(),&status,WUNTRACED);
      edit_bgjob_status(getfgpgid(),status);
      printf("[%d]   Stopped                 %s\n",jid,getfgcmd()->cmdline);
      set_waitfg();
    }
    else if (signo==SIGINT){
      set_waitfg();
    }
  }
  sigprocmask(SIG_UNBLOCK,&g_blocked,NULL);
  return;
}

