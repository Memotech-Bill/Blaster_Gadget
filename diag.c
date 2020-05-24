/* diag.h - Diagnostic code */

#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "diag.h"

static FILE *fLog = NULL;
static int diags = 0;

void diag_open (const char *psPath, int flags)
    {
    if ( flags )
        {
        printf ("Create log file %s\n", psPath);
        fLog = fopen (psPath, "w");
        if ( fLog == NULL )
            {
            int iErr = errno;
            printf ("Error %d: %s\n", iErr, strerror (iErr));
            }
        }
    diags = flags;
    }

void diag_message (int flag, const char *fmt, ...)
	{
    int iErr = 0;
    const char *psErr = NULL;
    if ( flag == DIAG_SYSERR )
        {
        iErr = errno;
        psErr = strerror (iErr);
        }
	if ( ( fLog != NULL ) && ( ( flag < 0 ) || ( diags & flag ) ) )
		{
		va_list	vars;
		va_start (vars, fmt);
		vfprintf (fLog, fmt, vars);
		va_end (vars);
        if ( flag == DIAG_GPIO ) fprintf (fLog, "\n");
        if ( iErr != 0 ) fprintf (fLog, "Error %d: %s\n", iErr, psErr);
        fflush (fLog);
		}
    else if ( flag < 0 )
        {
		va_list	vars;
		va_start (vars, fmt);
		vfprintf (stderr, fmt, vars);
		va_end (vars);
        if ( iErr != 0 ) fprintf (stderr, "Error %d: %s\n", iErr, psErr);
        fflush (stderr);
        }
    if ( flag <= DIAG_FATAL )
        {
        printf ("Fatal diagnostic\n");
        exit (1);
        }
	}

void diag_close (void)
    {
    if ( fLog != NULL ) fclose (fLog);
    fLog = NULL;
    }
