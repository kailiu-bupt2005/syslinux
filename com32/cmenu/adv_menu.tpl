All the lines in this file not in between --something BEGINS-- and --something ENDS--
is ignored completely and will not make it into the generated C file

This file has sections of C code each section delimited by --secname BEGINS--
and --secname ENDS--. In the generated C code certain section may be used multiple
times. Currently the different section which must be defined are

header, system, item, login and footer

Any additional sections you define will be processed but will probably not make it
to the C code if you do not modify menugen.py to use it.

header and footer go through unmolested. The remaining are % substituted using
python rules. Basically it means %(var)s gets replaced by the value of the variable
"var" which is a processed form of what is read from the .menu file

NOTE: There is absolutely no C code in the python script, so you are free to
modify this template to suit your needs

--header BEGINS--
/* -*- c -*- ------------------------------------------------------------- *
 *
 *   Copyright 2004-2006 Murali Krishnan Ganapathy - All Rights Reserved
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 53 Temple Place Ste 330,
 *   Boston MA 02111-1307, USA; either version 2 of the License, or
 *   (at your option) any later version; incorporated herein by reference.
 *
 * ----------------------------------------------------------------------- */

#ifndef NULL
#define NULL ((void *) 0)
#endif

#include "menu.h"
#include "help.h"
#include "passwords.h"
#include "com32io.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#define MAX_CMD_LINE_LENGTH 514

typedef struct s_xtra {
  long ipappend; // Stores the ipappend flag to send (useful for PXELINUX only)
  char *argsmenu; // Stores the name of menu which contains options for the given RUN item
  char *perms; // stores the permissions required to activate the item
} t_xtra;

typedef t_xtra *pt_xtra; // Pointer to extra datastructure

// Types of dot commands for which caller takes responsibility of handling
// In some case some commands may not make sense, it is up to the caller
// to handle cases which do not make sense
typedef enum {QUIT_CMD, REPEAT_CMD, ENTER_CMD, ESCAPE_CMD} t_dotcmd;


/*----------------- Global Variables */

// default user
#define GUEST_USER "guest"

// for local commands. return value of execdotcmd
#define QUIT_CMD 0
#define RPT_CMD 1

char username[12]; // Name of user currently using the system

int PWD_ROW; // Line number where user authentication happens
int EDIT_ROW; // row where User Tab

char loginstr[] = "<L>ogin  ";
char logoutstr[30];

int vmode; // The video mode we want to be in
char timeoutcmd[MAX_CMD_LINE_LENGTH]; // Command to execute on timeout
char totaltimeoutcmd[MAX_CMD_LINE_LENGTH]; // Command to execute on totaltimeout

char QUITSTR[] = ".quit"; // same as exit
char IGNORESTR[]=".ignore"; // same as repeat, wait

/*----------------  End globals */

// returns pointer to first non-space char
// and advances end of str by removing trailing spaces
char * strip(char *str)
{
  char *p,*s,*e;
  if (!str) return NULL;
  p = str;
  s = NULL;
  e = NULL;
  while (*p) {
    if (*p != ' ') {
       // mark start of string or record the last visited non-space char
       if (!s) s=p; else e=p;
    }
    p++;
  }
  *(++e)='\0'; // kill string earlier
  return s;
}

// executes a list of % separated commands
// non-dot commands are assumed to be syslinux commands
// All syslinux commands are appended with the contents of kerargs
// If it fails (kernel not found) then the next one is tried in the
// list
// returns QUIT_CMD or RPT_CMD
t_dotcmd execdotcmd(const char *cmd, char *defcmd, const char *kerargs)
{
   char cmdline[MAX_CMD_LINE_LENGTH];
   char dotcmd[MAX_CMD_LINE_LENGTH];
   char *curr,*next,*p,*args;
   char ctr;

   strcpy(dotcmd,cmd);
   next = dotcmd;
   cmdline[0] = '\0';
   while (*next) { // if something to do
      curr = next;
      p = strchr(next,'%');
      if (p) {
         *p--='\0'; next=p+2;
         while (*p == ' ') p--;
         *(++p)='\0'; // remove trailing spaces
      } else {
        if (*defcmd) { // execute defcmd next
            next=defcmd;
            defcmd=NULL; // exec def cmd only once
        } else next=NULL;
      }
      // now we just need to execute the command "curr"
      curr = strip(curr);
      if (curr[0] != '.') { // just run the kernel
         strcpy(cmdline,curr);
         if (kerargs) strcat(cmdline,kerargs);
         runsyslinuximage(cmdline,0); // No IPAppend
      } else { // We have a DOT command
        // split command into command and args (may be empty)
        args = curr;
        while ( (*args != ' ') && (*args != '\0') ) args++;
        if (*args) { // found a space
           *args++ = '\0';
           while (*args == ' ') args++; // skip over spaces
        }
        if ( (strcmp(curr,".exit")==0) ||
             (strcmp(curr,".quit")==0)
           )
           return QUIT_CMD;
        if ( (strcmp(curr,".repeat")==0) ||
             (strcmp(curr,".ignore")==0) ||
             (strcmp(curr,".wait")==0)
           )
           return RPT_CMD;
        if (strcmp(curr,".beep")==0) {
           if ((args) && ('0' <= args[0]) && (args[0] <= '9'))
              ctr = args[0]-'0';
           else ctr=1;
           for (;ctr>0; ctr--) beep();
        }
        if (strcmp(curr,".help")==0) runhelp(args);
      }
   }
   return RPT_CMD; // by default we do not quit
}


TIMEOUTCODE timeout(const char *cmd)
{
  t_dotcmd c;
  c = execdotcmd(cmd,".wait",NULL);
  switch(c) {
    case ENTER_CMD:
         return CODE_ENTER;
    case ESCAPE_CMD:
         return CODE_ESCAPE;
    default:
         return CODE_WAIT;
  }
}

TIMEOUTCODE ontimeout()
{
   return timeout(timeoutcmd);
}

TIMEOUTCODE ontotaltimeout()
{
   return timeout(totaltimeoutcmd);
}

void keys_handler(t_menuitem *mi,unsigned int scancode)
{
   int nc, nr;

   if (getscreensize(1, &nr, &nc)) {
       /* Unknown screen size? */
       nc = 80;
       nr = 24;
   }

   if ( ((scancode >> 8) == F1) && (mi->helpid != 0xFFFF) ) { // If scancode of F1 and non-trivial helpid
      runhelpsystem(mi->helpid);
   }

   // If user hit TAB, and item is an "executable" item
   // and user has privileges to edit it, edit it in place.
   if (((scancode & 0xFF) == 0x09) && (mi->action == OPT_RUN) &&
       (EDIT_ROW < nr) && (EDIT_ROW > 0) &&
       (isallowed(username,"editcmd") || isallowed(username,"root"))) {
     // User typed TAB and has permissions to edit command line
     gotoxy(EDIT_ROW,1);
     csprint("Command line:",0x07);
     editstring(mi->data,ACTIONLEN);
     gotoxy(EDIT_ROW,1);
     cprint(' ',0x07,nc-1);
   }
}

t_handler_return login_handler(t_menuitem *mi)
{
  (void)mi; // Unused
  char pwd[40];
  char login[40];
  int nc, nr;
  t_handler_return rv;

  rv = ACTION_INVALID;
  if (PWD_ROW < 0) return rv; // No need to authenticate

  if (mi->item == loginstr) { /* User wants to login */
    if (getscreensize(1, &nr, &nc)) {
        /* Unknown screen size? */
        nc = 80;
        nr = 24;
    }

    gotoxy(PWD_ROW,1);
    csprint("Enter Username: ",0x07);
    getstring(login, sizeof username);
    gotoxy(PWD_ROW,1);
    cprint(' ',0x07,nc);
    csprint("Enter Password: ",0x07);
    getpwd(pwd, sizeof pwd);
    gotoxy(PWD_ROW,1);
    cprint(' ',0x07,nc);

    if (authenticate_user(login,pwd))
    {
      strcpy(username,login);
      strcpy(logoutstr,"<L>ogout ");
      strcat(logoutstr,username);
      mi->item = logoutstr; // Change item to read "Logout"
      rv.refresh = 1; // refresh the screen (as item contents changed)
    }
    else strcpy(username,GUEST_USER);
  }
  else // User needs to logout
  {
    strcpy(username,GUEST_USER);
    strcpy(logoutstr,"");
    mi->item = loginstr;
  }

  return rv;
}

t_handler_return check_perms(t_menusystem *ms, t_menuitem *mi)
{
   char *perms;
   pt_xtra x;

   (void) ms; // To keep compiler happy

   x = (pt_xtra) mi->extra_data;
   perms = ( x ? x->perms : NULL);
   if (!perms) return ACTION_VALID;

   if (isallowed(username,"root") || isallowed(username,perms)) // If allowed
      return ACTION_VALID;
   else return ACTION_INVALID;
}

// Compute the full command line to add and if non-trivial
// prepend the string prepend to final command line
// Assume cmdline points to buffer long enough to hold answer
void gencommand(pt_menuitem mi, char *cmdline)
{
   pt_xtra x;
   cmdline[0] = '\0';
   strcat(cmdline,mi->data);
   x = (pt_xtra) mi->extra_data;
   if ( (x) && (x->argsmenu)) gen_append_line(x->argsmenu,cmdline);
}


// run the given command together with additional options which may need passing
void runcommand(pt_menuitem mi)
{
   char *line;
   pt_xtra x;
   long ipappend;

   line = (char *)malloc(sizeof(char)*MAX_CMD_LINE_LENGTH);
   gencommand(mi,line);
   x = (pt_xtra) mi->extra_data;
   ipappend = (x ? x->ipappend : 0);

   runsyslinuximage(line,x->ipappend);
   free(line);
}

// set the extra info for the specified menu item.
void set_xtra(pt_menuitem mi, const char *argsmenu, const char *perms, unsigned int helpid, long ipappend)
{
   pt_xtra xtra;
   int bad_argsmenu, bad_perms, bad_ipappend;

   mi->extra_data = NULL; // initalize
   mi->helpid = helpid; // set help id

   if (mi->action != OPT_RUN) return;

   bad_argsmenu = bad_perms = bad_ipappend = 0;
   if ( (argsmenu==NULL) || (strlen(argsmenu)==0)) bad_argsmenu = 1;
   if ( (perms==NULL) || (strlen(perms)==0)) bad_perms = 1;
   if ( ipappend==0) bad_ipappend = 1;

   if (bad_argsmenu && bad_perms && bad_ipappend) return;

   xtra = (pt_xtra) malloc(sizeof(t_xtra));
   mi->extra_data = (void *) xtra;
   xtra->argsmenu = xtra->perms = NULL;
   xtra->ipappend = ipappend;
   if (!bad_argsmenu) {
      xtra->argsmenu = (char *) malloc(sizeof(char)*(strlen(argsmenu)+1));
      strcpy(xtra->argsmenu,argsmenu);
   }
   if (!bad_perms) {
      xtra->perms = (char *) malloc(sizeof(char)*(strlen(perms)+1));
      strcpy(xtra->perms,perms);
      mi->handler = &check_perms;
   }
}

int main(void)
{
  pt_menuitem curr;
  char quit;
  char exitcmd[MAX_CMD_LINE_LENGTH];
  char exitcmdroot[MAX_CMD_LINE_LENGTH];
  char onerrcmd[MAX_CMD_LINE_LENGTH];
  char startfile[MAX_CMD_LINE_LENGTH];
  char *ecmd; // effective exit command or onerrorcmd
  char skipbits;
  char skipcmd[MAX_CMD_LINE_LENGTH];
  int timeout; // time in multiples of 0.1 seconds
  int totaltimeout; // time in multiples of 0.1 seconds
  t_dotcmd dotrv; // to store the return value of execdotcmd
  int temp;

  strcpy(username,GUEST_USER);
--header ENDS--
--system BEGINS--
/* ---- Initializing menu system parameters --- */
  vmode = %(videomode)s;
  skipbits = %(skipcondn)s;
  PWD_ROW = %(pwdrow)s;
  EDIT_ROW = %(editrow)s;
  strcpy(onerrcmd,"%(onerrorcmd)s");
  strcpy(exitcmd,"%(exitcmd)s");
  strcpy(exitcmdroot,"%(exitcmdroot)s");
  // If not specified exitcmdroot = exitcmd
  if (exitcmdroot[0] == '\0') strcpy(exitcmdroot,exitcmd);
  // Timeout stuff
  timeout = %(timeout)s;
  strcpy(timeoutcmd,"%(timeoutcmd)s");
  totaltimeout = %(totaltimeout)s;
  strcpy(totaltimeoutcmd,"%(totaltimeoutcmd)s");
  strcpy(startfile,"%(startfile)s");

  init_help("%(helpdir)s");
  init_passwords("%(pwdfile)s");
  init_menusystem("%(title)s");
  set_window_size(%(top)s,%(left)s,%(bot)s,%(right)s);

  // Register the ontimeout handler, with a time out of 10 seconds
  reg_ontimeout(ontimeout,timeout*10,0);
  reg_ontotaltimeout(ontotaltimeout,totaltimeout*10);

  // Register menusystem handlers
  reg_handler(HDLR_KEYS,&keys_handler);
/* ---- End of initialization --- */
--system ENDS--
--item BEGINS--

  curr = add_item("%(item)s","%(info)s",%(type)s,"%(data)s",%(state)d);
  set_xtra(curr,"%(argsmenu)s","%(perms)s",%(helpid)d,%(ipappend)d); // Set associated extra info
  set_shortcut(%(shortcut)s);
--item ENDS--
--login BEGINS--

  curr = add_item(loginstr,"Login/Logout of authentication system",OPT_RUN,NULL,0);
  curr->helpid = %(helpid)d;
  curr->handler = &login_handler;
--login ENDS--
--menu BEGINS--

/* ------- MENU %(name)s ----- */
  add_named_menu("%(name)s","%(title)s",-1);
--menu ENDS--
--footer BEGINS--
/* ------- END OF MENU declarations ----- */

// Check if we should skip the menu altogether
  quit = 0; // Dont skip the menu
  if (getshiftflags() & skipbits) { // we must skip the menu altogther and execute skipcmd
    dotrv = execdotcmd(skipcmd,".beep%.exit",NULL); // Worst case we beep and exit
    if (dotrv == QUIT_CMD) quit = 1;
  }

// Switch vide mode if required
   if (vmode != 0xFF) setvideomode(vmode);

// Do we have a startfile to display?
   if (startfile[0] != '\0') runhelp(startfile);

// The main loop
  while (quit == 0) { // As long as quit is zero repeat
     curr = showmenus(find_menu_num("main")); // Initial menu is the one called "main"

     if (curr) {
        if (curr->action == OPT_RUN) {
           ecmd = (char *)malloc(sizeof(char)*MAX_CMD_LINE_LENGTH);
           gencommand(curr,ecmd);
           temp = (curr->extra_data ? ((pt_xtra)curr->extra_data)->ipappend : 0);
           runsyslinuximage(ecmd,temp);
           // kernel not found so execute the appropriate dot command
           dotrv = execdotcmd(onerrcmd,".quit",ecmd); // pass bad cmdline as arg
           if (dotrv== QUIT_CMD) quit = 1;
           free(ecmd); ecmd = NULL;
        }
        else csprint("Error in programming!",0x07);
     } else {
        // find effective exit command
        ecmd = ( isallowed(username,"root") ? exitcmdroot : exitcmd);
        dotrv = execdotcmd(ecmd,".repeat",NULL);
        quit = (dotrv == QUIT_CMD ? 1 : 0); // should we exit now
     }
  }

// Deallocate space used and quit
  close_passwords();
  close_help();
  close_menusystem();
  return 0;
}

--footer ENDS--
