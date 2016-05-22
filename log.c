/* ========================================================================== */
/*                                                                            */
/*   log.c                                                                    */
/*   (c) 2016 TheDrake                                                        */
/*                                                                            */
/*   Description                                                              */
/*                                                                            */
/* ========================================================================== */

#include <sys/time.h>

#include <time.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <syslog.h>

#include "sdm120c.h"
#include "log.h"

/*--------------------------------------------------------------------------
    getCurTime
----------------------------------------------------------------------------*/
char* getCurTime()
{
    time_t curTimeValue;
    struct tm *ltime;
    static struct timeval _t;
    static struct timezone tz;
    static char CurTime[100];

    time(&curTimeValue);
    ltime = (struct tm *) localtime(&curTimeValue);
    gettimeofday(&_t, &tz);

    strftime(CurTime,100,"%Y%m%d-%H:%M:%S",ltime);
    sprintf(CurTime, "%s.%06d", CurTime,(int)_t.tv_usec);

    return CurTime;
}


/*--------------------------------------------------------------------------
    log_message
----------------------------------------------------------------------------*/
void log_message(const int log, const char* format, ...) {
    va_list args;
    char buffer[1024];
    static int bCmdlineSyslogged = 0;
    
    if (log) {
       va_start(args, format);
       vsnprintf(buffer, 1024, format, args);
       va_end(args);
    }
    
    if (log & debug_mask & DEBUG_STDERR) {
       fprintf(stderr, "%s: %s(%lu) ", getCurTime(), programName, PID);
       fprintf(stderr, buffer);
       fprintf(stderr, "\n");
    }
    
    if (log & debug_mask & DEBUG_SYSLOG) {
        openlog("sdm120c", LOG_PID|LOG_CONS, LOG_USER);
        if (!bCmdlineSyslogged) { 
            char versionbuffer[strlen(programName)+strlen(version)+3];
            snprintf(versionbuffer, strlen(programName)+strlen(version)+3, "%s v%s", programName, version);
            syslog(LOG_INFO, versionbuffer);
            char parent[80];
            snprintf(parent, sizeof(parent), "parent: %s(%lu)", PARENTCOMMAND, PPID);
            syslog(LOG_INFO, parent);
            syslog(LOG_INFO, cmdline);
            bCmdlineSyslogged++;
        }
        syslog(LOG_INFO, buffer);
        closelog();
    }
}

