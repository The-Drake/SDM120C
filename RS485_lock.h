/* ========================================================================== */
/*                                                                            */
/*   RS485_lock.h                                                                */
/*   (c) 2016 TheDrake                                                        */
/*                                                                            */
/*   Description                                                              */
/*                                                                            */
/* ========================================================================== */

#ifndef __RS485_LOCK_H__
#define __RS485_LOCK_H__

#ifdef __cplusplus
#define extern "C" {		/* respect c++ callers */
#endif

extern char *devLCKfile;
extern char *devLCKfileNew;

extern void LockSer(const char *szttyDevice, const long unsigned int PID, int debug_flag);
extern int  ClrSerLock(long unsigned int LckPID);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __RS485_LOCK_H__ */

