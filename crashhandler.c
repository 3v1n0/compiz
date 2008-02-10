/*
 *
 * Compiz crash handler plugin
 *
 * crashhandler.c
 *
 * Copyright : (C) 2006 by Dennis Kasprzyk
 * E-mail    : onestone@beryl-project.org
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>

#include <compiz-core.h>

#include "crashhandler_options.h"

static CompDisplay *cDisplay;

static void
crash_handler (int sig)
{
    if (sig == SIGSEGV || sig == SIGFPE || sig == SIGILL || sig == SIGABRT)
    {
	static int count = 0;

	if (++count > 1)
	    exit (1);

	// backtrace
	char cmd[1024];

	snprintf (cmd, 1024, 
		  "echo -e \"set prompt\nthread apply all bt full\n"
	      	  "echo \\\\\\n\necho \\\\\\n\nbt\nquit\" > /tmp/gdb.tmp;"
		  "gdb -q %s %i < /tmp/gdb.tmp | "
		  "grep -v \"No symbol table\" | "
		  "tee %s/compiz_crash-%i.out; rm -f /tmp/gdb.tmp; "
		  "echo \"\n[CRASH_HANDLER]: "
		  "\\\"%s/compiz_crash-%i.out\\\" created!\n\"",
		 programName, getpid (), crashhandlerGetDirectory (cDisplay),
		 getpid (), crashhandlerGetDirectory (cDisplay), getpid () );

	system (cmd);

	if (crashhandlerGetStartWm (cDisplay) )
	{
	    if (fork () == 0)
	    {
		setsid ();
		putenv (cDisplay->displayString);
		execl ("/bin/sh", "/bin/sh", "-c",
		       crashhandlerGetWmCmd (cDisplay), NULL);
		exit (0);
	    }
	}

	exit (1);
    }
}

static void
crashhandlerDisplayOptionChanged (CompDisplay                *d,
				  CompOption                 *opt,
				  CrashhandlerDisplayOptions num)
{
    switch (num)
    {

    case CrashhandlerDisplayOptionEnabled:
	if (crashhandlerGetEnabled (d) )
	{
	    // enable crash handler
	    signal (SIGSEGV, crash_handler);
	    signal (SIGFPE, crash_handler);
	    signal (SIGILL, crash_handler);
	    signal (SIGABRT, crash_handler);
	}
	else
	{
	    // disable crash handler
	    signal (SIGSEGV, SIG_DFL);
	    signal (SIGFPE, SIG_DFL);
	    signal (SIGILL, SIG_DFL);
	    signal (SIGABRT, SIG_DFL);
	}

	break;

    default:
	break;
    }
}

static Bool
crashhandlerInitDisplay (CompPlugin  *p,
			 CompDisplay *d)
{
    cDisplay = d;

    if (!checkPluginABI ("core", CORE_ABIVERSION))
	return FALSE;

    if (crashhandlerGetEnabled (d) )
    {
	// segmentation fault
	signal (SIGSEGV, crash_handler);
	// floating point exception
	signal (SIGFPE, crash_handler);
	// illegal instruction
	signal (SIGILL, crash_handler);
	// abort
	signal (SIGABRT, crash_handler);
    }

    crashhandlerSetEnabledNotify (d, crashhandlerDisplayOptionChanged);

    return TRUE;
}

static void
crashhandlerFiniDisplay (CompPlugin  *p,
			 CompDisplay *d)
{
    signal (SIGSEGV, SIG_DFL);
    signal (SIGFPE, SIG_DFL);
    signal (SIGILL, SIG_DFL);
    signal (SIGABRT, SIG_DFL);
}

static CompBool
crashhandlerInitObject (CompPlugin *p,
			CompObject *o)
{
    static InitPluginObjectProc dispTab[] = {
	(InitPluginObjectProc) 0, /* InitCore */
	(InitPluginObjectProc) crashhandlerInitDisplay
    };

    RETURN_DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), TRUE, (p, o));
}

static void
crashhandlerFiniObject (CompPlugin *p,
			CompObject *o)
{
    static FiniPluginObjectProc dispTab[] = {
	(FiniPluginObjectProc) 0, /* FiniCore */
	(FiniPluginObjectProc) crashhandlerFiniDisplay
    };

    DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), (p, o));
}


CompPluginVTable crashhandlerVTable = {
    "crashhandler",
    0,
    0,
    0,
    crashhandlerInitObject,
    crashhandlerFiniObject,
    0,
    0
};

CompPluginVTable *
getCompPluginInfo (void)
{
    return &crashhandlerVTable;
}
