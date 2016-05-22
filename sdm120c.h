/* ========================================================================== */
/*                                                                            */
/*   sdm120c.h                                                                */
/*   (c) 2016 TheDrake                                                        */
/*                                                                            */
/*   Description                                                              */
/*                                                                            */
/* ========================================================================== */

#ifndef __SDM120C_H__
#define __SDM120C_H__

#ifdef __cplusplus
#define extern "C" {		/* respect c++ callers */
#endif

#define DEBUG_STDERR 1
#define DEBUG_SYSLOG 2

extern char *programName;
extern const char *version;

extern int debug_flag;
extern int debug_mask;
extern int yLockWait;
extern long unsigned int PID;
extern long unsigned int PPID;
extern char *PARENTCOMMAND;
extern char cmdline[];

extern void *getMemPtr(size_t mSize);
extern int getIntLen(long value);
extern void *getPIDcmd(long unsigned int PID);
extern long inline tv_diff(struct timeval const * const t1, struct timeval const * const t2);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __SDM120C_H__ */

