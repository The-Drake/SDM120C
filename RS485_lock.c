/* ========================================================================== */
/*                                                                            */
/*   RS485_lock.c                                                             */
/*   (c) 2016 TheDrake                                                        */
/*                                                                            */
/*   Description                                                              */
/*                                                                            */
/* ========================================================================== */

// Enable checks for inter-lock problems debug
#define CHECKFORGHOSTAPPEND     0
#define CHECKFORCLEARLOCKRACE   0

#include <sys/types.h>
#include <sys/file.h>
#include <sys/time.h>

#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "sdm120c.h"
#include "log.h"

#if CHECKFORCLEARLOCKRACE
#include <glob.h>
#endif

const char *ttyLCKloc   = "/var/lock/LCK.."; /* location and prefix of serial port lock file */

char *devLCKfile = NULL;
char *devLCKfileNew = NULL;

/*--------------------------------------------------------------------------
        rnd_usleep
----------------------------------------------------------------------------*/
long inline rnd_usleep(const useconds_t usecs)
{
    long unsigned rnd10 = 10.0*rand()/(RAND_MAX+1.0) + 1;
    if (usleep(usecs*rnd10) == 0)
        return usecs*rnd10;
    else
        return -1;
}

/*--------------------------------------------------------------------------
    ClrSerLock
    Clear Serial Port lock.
----------------------------------------------------------------------------*/
int ClrSerLock(long unsigned int LckPID) {
    FILE *fdserlck, *fdserlcknew;
    long unsigned int PID;
    int bWrite, bRead;
    int errno_save = 0;
    int fLen = 0;
    int cmdLen = 0;
    int curChar = 0;
    char *COMMAND = NULL;

    errno = 0;
    log_message(debug_flag, "devLCKfile: <%s>", devLCKfile);
    log_message(debug_flag, "devLCKfileNew: <%s> ", devLCKfileNew);
    log_message(debug_flag, "Clearing Serial Port Lock (%lu)...", LckPID);
    
    fdserlck = fopen(devLCKfile, "r");
    if (fdserlck == NULL) {
        log_message(debug_flag | DEBUG_SYSLOG, "Problem opening serial device lock file to clear PID %lu: %s for read.",LckPID,devLCKfile);
        return(0);
    }
    log_message(debug_flag, "Acquiring exclusive lock on %s...",devLCKfile);
    flock(fileno(fdserlck), LOCK_EX);   // Will wait to acquire lock then continue
    log_message(debug_flag, "Exclusive lock on %s acquired (%d) %s...",devLCKfile, errno, strerror(errno));

#if CHECKFORCLEARLOCKRACE

    // Check for potential conflicts
    glob_t globbuf;
    int iGlob, fGlob = TRUE;
    
    log_message(debug_flag, "GlobCheck - Check to avoid simultaneous PID clearing");
    for (iGlob=5; iGlob>0 && fGlob; iGlob--) {
      fGlob = FALSE;      
      if (glob("/var/lock/LCK..ttyUSB0.*", GLOB_NOSORT, NULL, &globbuf) != GLOB_NOMATCH) {
          log_message(debug_flag | DEBUG_SYSLOG, "GlobCheck (%u), some other process is clearing lock too!!! (%s)",iGlob, globbuf.gl_pathv[0]);
          fGlob=TRUE;
          log_message(debug_flag, "Sleeping %ldus", rnd_usleep(500000));
      }
      globfree(&globbuf);
    }

#endif

    fdserlcknew = fopen(devLCKfileNew, "a");
    if (fdserlcknew == NULL) {
        log_message(debug_flag | DEBUG_SYSLOG, "Problem opening new serial device lock file to clear PID %lu: %s for write.",LckPID,devLCKfileNew);
        fclose(fdserlck);
        return(0);
    }
    
    // Find cmdLen max len in file
    curChar = 0;
    while (curChar != EOF) {
        fLen = 0;
        while ((curChar = fgetc(fdserlck)) != EOF && curChar != '\n' && curChar != ' ') fLen++;
        if (curChar == ' ') {
            fLen = 0;
            while ((curChar = fgetc(fdserlck)) != EOF && curChar != '\n') fLen++;
            if (fLen > cmdLen) cmdLen = fLen;
        }
    }
    rewind(fdserlck);
    
    log_message(debug_flag, "cmdLen=%i", cmdLen);
    COMMAND = getMemPtr(cmdLen+1);
    log_message(debug_flag, "cmdLen=%i COMMAND %s", cmdLen, (COMMAND==NULL ? "is null" : "is not null"));
    COMMAND[0] = '\0'; PID = 0;
    errno = 0;
    bRead = fscanf(fdserlck, "%lu%*[ ]%[^\n]\n", &PID, COMMAND);
    errno_save = errno;
    log_message(debug_flag, "errno=%i, bRead=%i LckPID=%lu PID=%lu COMMAND='%s'", errno_save, bRead, LckPID, PID, COMMAND);
    
    while (bRead != EOF && bRead > 0) {
        if (PID != LckPID) {
            errno = 0;
            if (COMMAND[0] != '\0') {
                bWrite = fprintf(fdserlcknew, "%lu %s\n", PID, COMMAND);
                errno_save = errno;
            } else {
                bWrite = fprintf(fdserlcknew, "%lu\n", PID);
                errno_save = errno;
            }
            log_message(debug_flag, "errno=%i, bWrite=%i PID=%lu", errno, bWrite, PID);
            if (bWrite < 0 || errno_save != 0) {
                log_message(debug_flag | DEBUG_SYSLOG, "Problem clearing serial device lock, can't write lock file: %s. %s",devLCKfile,strerror(errno_save));
                log_message(debug_flag | DEBUG_SYSLOG, "(%u) %s",errno_save,strerror(errno_save));
                fclose(fdserlcknew);
                return(0);
            }
        }
        errno=0; PID=0; COMMAND[0] = '\0';
        bRead = fscanf(fdserlck, "%lu%*[ ]%[^\n]\n", &PID, COMMAND);
        errno_save = errno;
        log_message(debug_flag, "errno=%i, bRead=%i LckPID=%lu PID=%lu COMMAND='%s'", errno_save, bRead, LckPID, PID, COMMAND);
    }
    
    fflush(fdserlcknew);

    errno = 0;
    if (rename(devLCKfileNew,devLCKfile)) { 
        log_message(debug_flag | DEBUG_SYSLOG, "Problem clearing serial device lock, can't update lock file: %s.",devLCKfile);
        log_message(debug_flag | DEBUG_SYSLOG, "(%d) %s", errno, strerror(errno));
    }

#if CHECKFORGHOSTAPPEND

    log_message(debug_flag, "Clearing Serial Port Lock almost done...");
    log_message(debug_flag, "Sleeping %luus", rnd_usleep(10000));

    // Check for latest appends (ghost appends)
    int iGhost=10;
    bRead = fscanf(fdserlck, "%lu%*[ ]%*[^\n]\n", &PID);
    while (iGhost > 0) {
        if (bRead > 0) {
            log_message(debug_flag | DEBUG_SYSLOG, "Found ghost append (%d): %s. %lu",iGhost,devLCKfile,PID);
            errno = 0;            
            bWrite = fprintf(fdserlcknew, "%lu\n", PID);
            errno_save = errno;
            if (bWrite < 0 || errno_save != 0) {
                log_message(debug_flag | DEBUG_SYSLOG, "Problem clearing serial device lock, can't write lock file: %s. %s",devLCKfile,strerror (errno_save));
                log_message(debug_flag | DEBUG_SYSLOG, "(%u) %s", errno_save, strerror(errno_save));
                fclose(fdserlcknew);
                return(0);
            }
        }
        fflush(fdserlcknew);
        log_message(debug_flag, "Sleeping %ldus", rnd_usleep(10000));
        iGhost--; PID=0;
        bRead = fscanf(fdserlck, "%lu%*[ ]%*[^\n]\n", &PID);
    }
    
#endif

    fclose(fdserlck);
    fclose(fdserlcknew);
    free(COMMAND);

    log_message(debug_flag, "Clearing Serial Port Lock done");

    return -1;
}

/*--------------------------------------------------------------------------
    AddSerLock
    Queue Serial Port lock intent.
----------------------------------------------------------------------------*/
void AddSerLock(const char *szttyDevice, const char *devLCKfile, const long unsigned int PID, char *COMMAND, const int debug_flag) {
    FILE *fdserlck;
    int bWrite;
    int errno_save = 0;

    log_message(debug_flag, "Attempting to get lock on Serial Port %s...",szttyDevice);
    do {
        fdserlck = fopen((const char *)devLCKfile, "a");
        if (fdserlck == NULL) {
            log_message(DEBUG_STDERR | DEBUG_SYSLOG, "Problem locking serial device, can't open lock file: %s for write.",devLCKfile);
            log_message(DEBUG_STDERR | DEBUG_SYSLOG, "Check owner and execution permission for '%s', they shoud be root '-rws--x--x'.",programName);
            exit(2);
        }
        log_message(debug_flag, "Acquiring shared lock on %s...",devLCKfile);
        errno = 0;
        if (flock(fileno(fdserlck), LOCK_SH | LOCK_NB) == 0) break;      // Lock Acquired 
        errno_save=errno;
        
        if (errno_save == EWOULDBLOCK) {
            log_message(debug_flag, "Would block %s, retry (%d) %s...", devLCKfile, errno_save, strerror(errno_save));
            rnd_usleep(25000);
            fclose(fdserlck);
        } else {
            log_message(DEBUG_STDERR | DEBUG_SYSLOG, "Problem locking serial device, can't open lock file: %s for write. (%d) %s", devLCKfile, errno_save, strerror(errno_save));
            exit(2);
        }
    } while (errno_save == EWOULDBLOCK);
    log_message(debug_flag, "Shared lock on %s acquired...",devLCKfile);
    
    errno=0;
    bWrite = fprintf(fdserlck, "%lu %s\n", PID, COMMAND);
    errno_save = errno;
    fflush(fdserlck);
    fclose(fdserlck);                   // Will release lock
    //fdserlck = NULL;
    if (bWrite < 0 || errno_save != 0) {
        log_message(debug_flag | DEBUG_SYSLOG, "Problem locking serial device, can't write lock file: %s.", devLCKfile);
        log_message(debug_flag | DEBUG_SYSLOG, "(%u) %s", devLCKfile, errno_save, strerror(errno_save));
        exit(2);
    }
}

/*--------------------------------------------------------------------------
    LockSer
----------------------------------------------------------------------------*/
void LockSer(const char *szttyDevice, const long unsigned int PID, int debug_flag)
{
    char *pos;
    FILE *fdserlck = NULL;
    char *COMMAND = NULL;
    long unsigned int LckPID;
    struct timeval tLockStart, tLockNow;
    int bRead;
    int errno_save = 0;
    int fLen = 0;
    int curChar = 0;
    char *LckCOMMAND = NULL;
    char *LckPIDcommand = NULL;

    pos = strrchr(szttyDevice, '/');
    if (pos > 0) {
        pos++;
        devLCKfile = getMemPtr(strlen(ttyLCKloc)+(strlen(szttyDevice)-(pos-szttyDevice))+1);
        devLCKfile[0] = '\0';
        strcpy(devLCKfile,ttyLCKloc);
        strcat(devLCKfile, pos);
        devLCKfile[strlen(devLCKfile)] = '\0';
        devLCKfileNew = getMemPtr(strlen(devLCKfile)+getIntLen(PID)+2);	/* dot & terminator */
        devLCKfileNew[0] = '\0';
        strcpy(devLCKfileNew,devLCKfile);
        sprintf(devLCKfileNew,"%s.%lu",devLCKfile,PID);
        devLCKfileNew[strlen(devLCKfileNew)] = '\0';
    } else {
        devLCKfile = NULL;
    }

    log_message(debug_flag, "szttyDevice: %s",szttyDevice);
    log_message(debug_flag, "devLCKfile: <%s>",devLCKfile);
    log_message(debug_flag, "devLCKfileNew: <%s>",devLCKfileNew);
    log_message(debug_flag, "PID: %lu", PID);    

    COMMAND = getPIDcmd(PID);
    AddSerLock(szttyDevice, devLCKfile, PID, COMMAND, debug_flag);

    LckPID = 0;
    long unsigned int oldLckPID = 0;
    int staleLockRetries = 0;
    int const staleLockRetriesMax = 2;
    long unsigned int clrStaleTargetPID = 0;    
    int missingPidRetries = 0;
    int const missingPidRetriesMax = 2;
    
    gettimeofday(&tLockStart, NULL);
    tLockNow=tLockStart;

    if (debug_flag) log_message(debug_flag, "Checking for lock");
    while(LckPID != PID && tv_diff(&tLockNow, &tLockStart) <= yLockWait*1000000L) {

        do {
            fdserlck = fopen(devLCKfile, "r");
            if (fdserlck == NULL) {
                log_message(debug_flag | DEBUG_SYSLOG, "Problem locking serial device, can't open lock file: %s for read.",devLCKfile);
                exit(2);
            }
            //log_message(debug_flag, "Acquiring shared lock on %s...",devLCKfile);
            errno = 0;
            if (flock(fileno(fdserlck), LOCK_SH | LOCK_NB) == 0) break;      // Lock Acquired 
            errno_save=errno;
            
            if (errno_save == EWOULDBLOCK) {
                log_message(debug_flag, "Would block %s, retry (%d) %s...", devLCKfile, errno_save, strerror(errno_save));
                rnd_usleep(25000);
                fclose(fdserlck);
            } else {
                log_message(DEBUG_STDERR | DEBUG_SYSLOG, "Problem locking serial device, can't open lock file: %s for read. (%d) %s", devLCKfile, errno_save, strerror(errno_save));
                exit(2);
            }
        } while (errno_save == EWOULDBLOCK);
        //log_message(debug_flag, "Shared lock on %s acquired...",devLCKfile);

        fLen = 0;
        while ((curChar = fgetc(fdserlck)) != EOF && curChar != '\n' && curChar != ' ') fLen++;
        fLen = 0;
        if (curChar == ' ') while ((curChar = fgetc(fdserlck)) != EOF && curChar != '\n') fLen++;

        rewind(fdserlck);
        
        //if (LckPID != oldLckPID) log_message(debug_flag, "fLen=%i", fLen);
        LckCOMMAND = getMemPtr(fLen+1);
        //if (LckPID != oldLckPID) log_message(debug_flag, "fLen=%i LckCOMMAND %s", fLen, (LckCOMMAND==NULL ? "is null" : "is not null"));
        LckCOMMAND[0] = '\0';
        LckPID=0;
        
        errno = 0;
        bRead = fscanf(fdserlck, "%lu%*[ ]%[^\n]\n", &LckPID, LckCOMMAND);
        errno_save = errno;
        fclose(fdserlck);
        if (LckPID != oldLckPID) {
            log_message(debug_flag | (bRead==EOF || errno_save != 0 ? DEBUG_SYSLOG : 0), "errno=%i, bRead=%i PID=%lu LckPID=%lu", errno_save, bRead, PID, LckPID);
            log_message(debug_flag, "Checking process %lu (%s) for lock", LckPID, LckCOMMAND);
            //oldLckPID = LckPID;
        }
        if (bRead == EOF || LckPID == 0 || errno_save != 0) {
            log_message(debug_flag | DEBUG_SYSLOG, "Problem locking serial device, can't read PID from lock file: %s.",devLCKfile);
            log_message(debug_flag | DEBUG_SYSLOG, "errno=%i, bRead=%i PID=%lu LckPID=%lu", errno_save, bRead, PID, LckPID);
            if (errno_save != 0) {
                // Real error 
                log_message(debug_flag | DEBUG_SYSLOG, "(%u) %s", errno_save, strerror(errno_save));
                free(LckCOMMAND); free(LckPIDcommand); free(COMMAND);
                exit(2);
            } else {
                if (missingPidRetries < missingPidRetriesMax) {
                    missingPidRetries++;
                    log_message(debug_flag, "%s miss process self PID from lock file?",devLCKfile);
                } else if (missingPidRetries >= missingPidRetriesMax) {
                    // Self PID missing... (Should never happen)
                    log_message(debug_flag | DEBUG_SYSLOG, "%s miss process self PID from lock file, amending.",devLCKfile);
                    AddSerLock(szttyDevice, devLCKfile, PID, COMMAND, debug_flag);
                    //LckPID=0;
                    missingPidRetries = 0;
                }
            }
            oldLckPID = LckPID;
        } else { //fread OK
          
          // We got a pid from lockfile, let's clear missing pid status
          missingPidRetries = 0;
          
          LckPIDcommand = getPIDcmd(LckPID);
          
          if (LckPID != oldLckPID) {
              log_message(debug_flag, "PID: %lu COMMAND: \"%s\" LckPID: %lu LckCOMMAND: \"%s\" LckPIDcommand \"%s\"%s", PID, COMMAND
                                          , LckPID, LckCOMMAND, LckPIDcommand
                                          , LckPID == PID ? " = me" : "");
              oldLckPID = LckPID;              
          }
          
//        PID           - this process
//        LckPID        - PID from lock file
//        COMMAND       - this process
//        LckCOMMAND    - process command from lock file
//        LckPIDcommand - process command of process using PID from lock file
          if ((PID != LckPID && LckPIDcommand == NULL) || (LckCOMMAND[0]!='\0' && strcmp(LckPIDcommand,LckCOMMAND) != 0) || strcmp(LckPIDcommand,"") == 0) {
                // Is it a stale lock pid?
                if (staleLockRetries < staleLockRetriesMax) {
                    staleLockRetries++;
                    clrStaleTargetPID = LckPID;
                    log_message(debug_flag | (staleLockRetries > 1 ? DEBUG_SYSLOG : 0), "Stale pid lock(%d)? PID=%lu, LckPID=%lu, LckCOMMAND='%s', LckPIDCommand='%s'", staleLockRetries, PID, LckPID, LckCOMMAND, LckPIDcommand);
                } else if (LckPID == clrStaleTargetPID && staleLockRetries >= staleLockRetriesMax) {
                    log_message(debug_flag | DEBUG_SYSLOG, "Clearing stale serial port lock. (%lu)", LckPID);
                    ClrSerLock(LckPID);
                    staleLockRetries = 0;
                    clrStaleTargetPID = 0;
                }
          } else {
                // Pid lock have a process running, let's reset stale pid retries
                staleLockRetries = 0;
                clrStaleTargetPID = 0;
          } 
        }

        if (yLockWait > 0 && LckPID != PID) {
             rnd_usleep(25000);
             //log_message(debug_flag, "Sleeping %luus", rnd_usleep(25000));
        }

        // Cleanup and loop        
        if (LckCOMMAND != NULL) {
            free(LckCOMMAND);
            LckCOMMAND = NULL;
        }
        if (LckPIDcommand != NULL) {
            free(LckPIDcommand);
            LckPIDcommand = NULL;
        }
        gettimeofday(&tLockNow, NULL);
    } // while
    free(COMMAND);
    if (LckPID == PID) log_message(debug_flag, "Appears we got the lock.");
    if (LckPID != PID) {
        ClrSerLock(PID);
        log_message(DEBUG_STDERR, "Problem locking serial device %s.",szttyDevice);
        log_message(DEBUG_STDERR | DEBUG_SYSLOG, "Unable to get lock on serial %s for %lu in %ds: still locked by %lu.",szttyDevice,PID,(yLockWait)%30,LckPID);
        log_message(DEBUG_STDERR, "Try a greater -w value (eg -w%u).", (yLockWait+2)%30);
        free(devLCKfile); free(devLCKfileNew); free(PARENTCOMMAND);        
        exit(2);
    }
}
