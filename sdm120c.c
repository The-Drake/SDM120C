#ifdef __cplusplus
extern "C" {
#endif

/*
 * sdm120c: ModBus RTU client to read EASTRON SDM120C smart mini power meter registers
 *
 * Copyright (C) 2015 Gianfranco Di Prinzio <gianfrdp@inwind.it>
 * 
 * Locking code partially from aurora by Curtronis.
 * Some code by TheDrake too. :)  
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <sys/types.h>
#include <sys/file.h>
#include <sys/time.h>

#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <getopt.h>
#include <syslog.h>

#include <modbus-version.h>
#include <modbus.h>

#include "sdm120c.h"
#include "RS485_lock.h"
#include "log.h"

#define DEFAULT_RATE 2400

#define MODEL_120 1
#define MODEL_220 2

// Read
#define VOLTAGE   0x0000
#define CURRENT   0x0006
#define POWER     0x000C
#define APOWER    0x0012
#define RAPOWER   0x0018
#define PFACTOR   0x001E
#define PANGLE    0x0024
#define FREQUENCY 0x0046
#define IAENERGY  0x0048
#define EAENERGY  0x004A
#define IRAENERGY 0x004C
#define ERAENERGY 0x004E
#define TAENERGY  0x0156
#define TRENERGY  0x0158

uint16_t RTU_ReadRegistersBuffer[0x50]; // 0x50 = Max regs in 1 RTU transaction
unsigned char RTU_ReadRegistersRequests[0x50/2]; // Registers to read
unsigned char RTU_ReadRegistersAvailable[0x50/2]; // Registers already read

// Write
#define NPARSTOP  0x0012
#define DEVICE_ID 0x0014
#define BAUD_RATE 0x001C
#define TIME_DISP_220 0xF500
#define TIME_DISP 0xF900
#define TOT_MODE  0xF920

#define BR1200 5
#define BR2400 0
#define BR4800 1
#define BR9600 2

#define MAX_RETRIES 100

#define E_PARITY 'E'
#define O_PARITY 'O'
#define N_PARITY 'N'

#define RESTART_TRUE  1
#define RESTART_FALSE 0

int debug_mask     = DEBUG_STDERR | DEBUG_SYSLOG; // Default, let pass all
int debug_flag     = 0;
int trace_flag     = 0;

int metern_flag    = 0;

const char *version     = "1.4";
char *programName;

#define CMDLINESIZE 128            /* should be enough for debug */
char cmdline[CMDLINESIZE]="";    
long unsigned int PID;

long unsigned int PPID;
char *PARENTCOMMAND = NULL;

int yLockWait = 0;                 /* Seconds to wait to lock serial port */
static time_t command_delay = -1;  /* MilliSeconds to wait before sending a command */
static time_t settle_time = -1;    /* us to wait line to settle before starting chat */

unsigned long TotalModbusTime = 0L;

void usage(char* program) {
    printf("sdm120c %s: ModBus RTU client to read EASTRON SDM120C smart mini power meter registers\n",version);
    printf("Copyright (C) 2015 Gianfranco Di Prinzio <gianfrdp@inwind.it>\n");
    printf("Complied with libmodbus %s\n\n", LIBMODBUS_VERSION_STRING);
    printf("Usage: %s [-a address] [-d] [-x] [-p] [-v] [-c] [-e] [-i] [-t] [-f] [-g] [-T] [[-m]|[-q]] [-b baud_rate] [-P parity] [-S bit] [-z num_retries] [-j seconds] [-w seconds] [-1 | -2] device\n", program);
    printf("       %s [-a address] [-d] [-x] [-b baud_rate] [-P parity] [-S bit] [-1 | -2] [-z num_retries] [-j seconds] [-w seconds] -s new_address device\n", program);
    printf("       %s [-a address] [-d] [-x] [-b baud_rate] [-P parity] [-S bit] [-1 | -2] [-z num_retries] [-j seconds] [-w seconds] -r baud_rate device \n", program);
    printf("       %s [-a address] [-d] [-x] [-b baud_rate] [-P parity] [-S bit] [-1 | -2] [-z num_retries] [-j seconds] [-w seconds] -R new_time device\n\n", program);
    printf("Required:\n");
    printf("\tdevice\t\tSerial device (i.e. /dev/ttyUSB0)\n");
    printf("Connection parameters:\n");
    printf("\t-a address \tMeter number (1-247). Default: 1\n");
    printf("\t-b baud_rate \tUse baud_rate serial port speed (1200, 2400, 4800, 9600)\n");
    printf("\t\t\tDefault: 2400\n");
    printf("\t-P parity \tUse parity (E, N, O)\n");
    printf("\t-S bit \t\tUse stop bits (1, 2). Default: 1\n");
    printf("\t-1 \t\tModel: SDM120C (default)\n");
    printf("\t-2 \t\tModel: SDM220\n");
    printf("Reading parameters (no parameter = retrieves all values):\n");
    printf("\t-p \t\tGet power (W)\n");
    printf("\t-v \t\tGet voltage (V)\n");
    printf("\t-c \t\tGet current (A)\n");
    printf("\t-l \t\tGet apparent power (VA)\n");
    printf("\t-n \t\tGet reactive power (VAR)\n");
    printf("\t-f \t\tGet frequency (Hz)\n");
    printf("\t-o \t\tGet phase angle (Degree)\n");
    printf("\t-g \t\tGet power factor\n");
    printf("\t-i \t\tGet imported energy (Wh)\n");
    printf("\t-e \t\tGet exported energy (Wh)\n");
    printf("\t-t \t\tGet total energy (Wh)\n");
    printf("\t-A \t\tGet imported reactive energy (VARh)\n");
    printf("\t-B \t\tGet exported reactive energy (VARh)\n");
    printf("\t-C \t\tGet total reactive energy (VARh)\n");
    printf("\t-T \t\tGet Time for rotating display values (0=no rotation)\n");
    printf("\t-m \t\tOutput values in IEC 62056 format ID(VALUE*UNIT)\n");
    printf("\t-q \t\tOutput values in compact mode\n");
    printf("Writing new settings parameters:\n");
    printf("\t-s new_address \tSet new meter number (1-247)\n");
    printf("\t-r baud_rate \tSet baud_rate meter speed (1200, 2400, 4800, 9600)\n");
    printf("\t-N parity \tSet parity and stop bits (0-3)\n");
    printf("\t\t\t0: N1, 1: E1, 2: O1, 3:N2\n");
    printf("\t-R new_time  \tSet rotation time for displaying values (0=no rotation)\n");
    printf("\t\t\tSDM120: (0-30s)\n");
    printf("\t\t\tSDM120: (m-m-s-m) Demand interval, Slide time, Scroll time, Backlight time\n"); 
    printf("\t-M new_mmode \tSet total energy measurement mode (1-3)\n");
    printf("\t\t\t1: Total=Import, 2: Total=Import+Export, 3: Total=Import-Export\n");
    printf("Fine tuning & debug parameters:\n");
    printf("\t-z num_retries\tTry to read max num_retries times on bus before exiting\n");
    printf("\t\t\twith error. Default: 1 (no retry)\n");
    printf("\t-j 1/10 secs\tResponse timeout. Default: 2=0.2s\n");
    printf("\t-D 1/1000 secs\tDelay before sending commands. Default: 0ms\n");
    printf("\t-w seconds\tTime to wait to lock serial port (1-30s). Default: 0s\n");
    printf("\t-W 1/1000 secs\tTime to wait for 485 line to settle. Default: 0ms\n");
    printf("\t-y 1/1000 secs\tSet timeout between every bytes (1-500). Default: disabled\n");
    printf("\t-d debug_level\tDebug (0=disable, 1=debug, 2=errors to syslog, 3=both)\n");
    printf("\t\t\tDefault: Fatal errors to syslog + fatal errors to stderr\n");
    printf("\t-x \t\tTrace (libmodbus debug on)\n");
}

/*--------------------------------------------------------------------------
    tv_diff
----------------------------------------------------------------------------*/
long inline tv_diff(struct timeval const * const t1, struct timeval const * const t2)
{
    struct timeval res;
    timersub(t1, t2, &res);
    return res.tv_sec*1000000 + res.tv_usec;
}

/*--------------------------------------------------------------------------
    getCmdLine
----------------------------------------------------------------------------*/
void getCmdLine()
{
    int fd = open("/proc/self/cmdline", O_RDONLY);
    int nbytesread = read(fd, cmdline, CMDLINESIZE);
    char *p;
    if (nbytesread>0) {
        for (p=cmdline; p < cmdline+nbytesread; p++) if (*p=='\0') *p=' '; 
        cmdline[nbytesread-1]='\0';
    } else
        cmdline[0]='\0';
    close(fd);
}

/*--------------------------------------------------------------------------
    getMemPtr
----------------------------------------------------------------------------*/
void *getMemPtr(size_t mSize)
{
    void *ptr;

    ptr = calloc(sizeof(char),mSize);
    if (!ptr) {
        log_message(debug_flag | LOG_SYSLOG, "malloc failed");
        exit(2);
    }
    //cptr = (char *)ptr;
    //for (i = 0; i < mSize; i++) cptr[i] = '\0';
    return ptr;
}


void exit_error(modbus_t *ctx)
{
/*
      // Wait for line settle
      log_message(debug_flag, "Sleeping %dms for line settle...", settle_time);
      usleep(1000 * settle_time);
      log_message(debug_flag, "Flushed %d bytes", modbus_flush(ctx));
*/
      modbus_close(ctx);
      modbus_free(ctx);
      ClrSerLock(PID);
      free(devLCKfile);
      free(devLCKfileNew);
      if (!metern_flag) {
        printf("NOK\n");
        log_message(debug_flag | DEBUG_SYSLOG, "NOK");
      }
      free(PARENTCOMMAND);
      exit(EXIT_FAILURE);
}

inline int bcd2int(int val)
{
    return((((val & 0xf0) >> 4) * 10) + (val & 0xf));
}

int int2bcd(int val)
{
    return(((val / 10) << 4) + (val % 10));
}

inline float reform_uint16_2_float32(uint16_t u1, uint16_t u2)
{
  uint32_t num = ((uint32_t)u1 & 0xFFFF) << 16 | ((uint32_t)u2 & 0xFFFF);
    float numf;
    memcpy(&numf, &num, 4);
    return numf;
}

int bcd2num(const uint16_t *src, int len)
{
    int n = 0;
    int m = 1;
    int i = 0;
    int shift = 0;
    int digit = 0;
    int j = 0;
    for (i = 0; i < len; i++) {
        for (j = 0; j < 4; j++) {
            digit = ((src[len-1-i]>>shift) & 0x0F) * m;
            n += digit;
            m *= 10;
            shift += 4;
        }
    }
    return n;
}

#if 0

// unused

int getMeasureBCD(modbus_t *ctx, int address, int retries, int nb) {

    uint16_t tab_reg[nb * sizeof(uint16_t)];
    int rc;
    int i;
    int j = 0;
    int exit_loop = 0;

    while (j < retries && exit_loop == 0) {
      j++;

      if (command_delay) {
        log_message(debug_flag, "Sleeping command delay: %ldus", command_delay);
        usleep(command_delay);
      }

      log_message(debug_flag, "%d/%d. Register Address %d [%04X]", j, retries, 30000+address+1, address);
      rc = modbus_read_input_registers(ctx, address, nb, tab_reg); // will wait response_timeout for a reply

      if (rc == -1) {
        log_message(debug_flag | ( j==retries ? DEBUG_SYSLOG : 0), "%s: ERROR (%d) %s, %d/%d, Address %d [%04X]", errno, modbus_strerror(errno), j, retries, 30000+address+1, address);
        /* libmodbus already flushes 
        log_message(debug_flag, "Flushed %d bytes", modbus_flush(ctx));
        */
        if (command_delay) {
          log_message(debug_flag, "Sleeping command delay: %ldus", command_delay);
          usleep(command_delay);
        }
      } else {
        exit_loop = 1;
      }
    }

    if (rc == -1) {
      exit_error(ctx);
    }

    if (debug_flag) {
       for (i=0; i < rc; i++) {
          log_message(debug_flag, "reg[%d/%d]=%d (0x%X)", i, (rc-1), tab_reg[i], tab_reg[i]);
       }
    }

    int value = bcd2num(&tab_reg[0], rc);

    return value;
}

#endif

float getMeasureFloat(modbus_t *ctx, int address, int retries, int nb) {

    uint16_t tab_reg[nb * sizeof(uint16_t)];
    int rc = -1;
    int i;
    int j = 0;
    int exit_loop = 0;
    int errno_save=0;
    struct timeval tvStart, tvStop;

    if (debug_flag) log_message(debug_flag, "getMeasureFloat(), registry=%d [0x%04X]", address, address);

    if (RTU_ReadRegistersAvailable[address/2]==1) {
        return reform_uint16_2_float32(RTU_ReadRegistersBuffer[address], RTU_ReadRegistersBuffer[address+1]);
    }

    while (j < retries && exit_loop == 0) {
      j++;

      if (command_delay) {
        log_message(debug_flag, "Sleeping command delay: %ldus", command_delay);
        usleep(command_delay);
      }

      log_message(debug_flag, "%d/%d. Register Address %d [%04X], bufsize=%d", j, retries, 30000+address+1, address, nb);
      gettimeofday(&tvStart, NULL); 
      rc = modbus_read_input_registers(ctx, address, nb, tab_reg);
      errno_save = errno;
      gettimeofday(&tvStop, NULL); 

      if (rc == -1) {
        if (trace_flag) fprintf(stderr, "%s: ERROR (%d) %s, %d/%d\n", programName, errno_save, modbus_strerror(errno_save), j, retries);
        log_message(debug_flag | ( j==retries ? DEBUG_SYSLOG : 0), "ERROR (%d) %s, %d/%d, Address %d [%04X]", errno_save, modbus_strerror(errno_save), j, retries, 30000+address+1, address);
        log_message(debug_flag | ( j==retries ? DEBUG_SYSLOG : 0), "Response timeout gave up after %ldus", tv_diff(&tvStop, &tvStart));
        /* libmodbus already flushes 
        log_message(debug_flag, "Flushing modbus buffer");
        log_message(debug_flag, "Flushed %d bytes", modbus_flush(ctx));
        */
        if (command_delay) {
          log_message(debug_flag, "Sleeping command delay: %ldus", command_delay);
          usleep(command_delay);
        }
      } else {
        unsigned long tmp = tv_diff(&tvStop, &tvStart);
        log_message(debug_flag, "Reading OK: %d register(s) in %ldus time", rc, tmp);
        TotalModbusTime += tmp;
        exit_loop = 1;
      }

    }

    if (rc == -1) {
      exit_error(ctx);
    }

    if (debug_flag) {
       for (i=0; i < rc; i++) {
          log_message(debug_flag, "reg[%d/%d]=%d (0x%X)", i, (rc-1), tab_reg[i], tab_reg[i]);
       }
    }

/*
    // swap LSB and MSB
    uint16_t tmp1 = tab_reg[0];
    uint16_t tmp2 = tab_reg[1];
    tab_reg[0] = tmp2;
    tab_reg[1] = tmp1;

    float value = modbus_get_float(&tab_reg[0]);

    return value;
*/

    return reform_uint16_2_float32(tab_reg[0], tab_reg[1]);

}

void readRegisters(modbus_t *ctx, int offset, int nregs, uint16_t RTU_ReadRegistersBuffer[], unsigned char RTU_ReadRegistersRequests[], int retries)
{
    int rc = -1;
    int i;
    int j = 0;
    int exit_loop = 0;
    int errno_save=0;
    struct timeval tvStart, tvStop;
    int startreg, endreg;
    
    // todo: Optimize reading
    
    startreg=0;
    //log_message(debug_flag, "startreg=%d", startreg);
    for (i=0; i<nregs/2; i++) {
        if (RTU_ReadRegistersRequests[i] != 0) {
                startreg=i;
                break;
        }
        //log_message(debug_flag, "%d, startreg=%d", i, startreg);
    }

    endreg=nregs/2-1;
    //log_message(debug_flag, "endreg=%d", endreg);
    for (i=nregs/2-1; i>=0; i--) {
        if (RTU_ReadRegistersRequests[i] != 0) {
                endreg=i;
                break;
        }
        //log_message(debug_flag, "%d, endreg=%d", i, endreg);
    }
    log_message(debug_flag, "nregs=%d, startreg=%d, endreg=%d, bufsize=%d", nregs, startreg, endreg, (1+endreg-startreg)*2);
    
    while (j < retries && exit_loop == 0) {
      j++;

      if (command_delay) {
        log_message(debug_flag, "Sleeping command delay: %ldus", command_delay);
        usleep(command_delay);
      }

      log_message(debug_flag, "%d/%d. Register Address %d [0x%04X], bufsize=%d", j, retries, 30000+startreg*2+1, startreg*2, (endreg-startreg+1)*2);
      gettimeofday(&tvStart, NULL); 
      rc = modbus_read_input_registers(ctx, offset+startreg*2, (endreg-startreg+1)*2, &RTU_ReadRegistersBuffer[startreg*2]);
      errno_save = errno;
      gettimeofday(&tvStop, NULL); 

      if (rc == -1) {
        if (trace_flag) fprintf(stderr, "%s: ERROR (%d) %s, %d/%d\n", programName, errno_save, modbus_strerror(errno_save), j, retries);
        log_message(debug_flag | ( j==retries ? DEBUG_SYSLOG : 0), "ERROR (%d) %s, %d/%d, Address %d [0x%04X]", errno_save, modbus_strerror(errno_save), j, retries, 30000+(startreg*2)+1, startreg);
        log_message(debug_flag | ( j==retries ? DEBUG_SYSLOG : 0), "Response timeout gave up after %ldus", tv_diff(&tvStop, &tvStart));
        /* libmodbus already flushes 
        log_message(debug_flag, "Flushing modbus buffer");
        log_message(debug_flag, "Flushed %d bytes", modbus_flush(ctx));
        */
        if (command_delay) {
          log_message(debug_flag, "Sleeping command delay: %ldus", command_delay);
          usleep(command_delay);
        }
      } else {
        unsigned long tmp = tv_diff(&tvStop, &tvStart);
        log_message(debug_flag, "Reading OK: %d register(s) in %ldus time", rc, tmp);
        TotalModbusTime += tmp;
        exit_loop = 1;
      }

    }

    if (rc == -1) {
      exit_error(ctx);
    }

    for (i=0; i < rc; i++) {
        RTU_ReadRegistersAvailable[startreg+i]=1;
        //if (debug_flag) log_message(debug_flag, "reg[0x%04X] is marked read", (startreg+i)*2);
        if (debug_flag) log_message(debug_flag, "reg(%d/%d)[0x%04X]=%d [0x%04X]"
                                    , i+1, rc, (startreg+i)*2
                                    , RTU_ReadRegistersBuffer[startreg*2+i]
                                    , RTU_ReadRegistersBuffer[startreg*2+i]);
    }

 }

int getConfigBCD(modbus_t *ctx, int address, int retries, int nb) {

    uint16_t tab_reg[nb * sizeof(uint16_t)];
    int rc = -1;
    int i;
    int j = 0;
    int exit_loop = 0;

    while (j < retries && exit_loop == 0) {
      j++;

      if (command_delay) {
        log_message(debug_flag, "Sleeping command delay: %ldus", command_delay);
        usleep(command_delay);
      }

      log_message(debug_flag, "%d/%d. Register Address %d [%04X]", j, retries, 400000+address+1, address);
      rc = modbus_read_registers(ctx, address, nb, tab_reg);

      if (rc == -1) {
        log_message(debug_flag | ( j==retries ? DEBUG_SYSLOG : 0), "ERROR (%d) %s, %d/%d, Address %d [%04X]", errno, modbus_strerror(errno), j, retries, 30000+address+1, address);
        /* libmodbus already flushes 
        log_message(debug_flag, "Flushing modbus buffer");
        log_message(debug_flag, "Flushed %d bytes", modbus_flush(ctx));
        */
        if (command_delay) {
          log_message(debug_flag, "Sleeping command delay: %ldus", command_delay);
          usleep(command_delay);
        }
      } else {
        exit_loop = 1;
      }
    }

    if (rc == -1) {
      exit_error(ctx);
    }

    if (debug_flag) {
       for (i=0; i < rc; i++) {
          log_message(debug_flag, "reg[%d/%d]=%d (0x%X)", i, (rc-1), tab_reg[i], tab_reg[i]);
       }
    }

    int value = bcd2num(&tab_reg[0], rc);

    return value;

}

void changeConfigHex(modbus_t *ctx, int address, int new_value, int restart)
{
    uint16_t tab_reg[1];
    tab_reg[0] = new_value;

    if (command_delay) {
      log_message(debug_flag, "Sleeping command delay: %ldus", command_delay);
      usleep(command_delay);
    }

    int n = modbus_write_registers(ctx, address, 1, tab_reg);
    if (n != -1) {
        printf("New value %d for address 0x%X\n", new_value, address);
        if (restart == RESTART_TRUE) printf("You have to restart the meter for apply changes\n");
    } else {
        log_message(DEBUG_STDERR | DEBUG_SYSLOG, "error: (%d) %s, %d, %d", errno, modbus_strerror(errno), n);
        if (errno == EMBXILFUN) // Illegal function
            log_message(DEBUG_STDERR | DEBUG_SYSLOG, "Tip: is the meter in set mode?");
        exit_error(ctx);
    }
}

void changeConfigFloat(modbus_t *ctx, int address, int new_value, int restart, int nb)
{
    uint16_t tab_reg[nb * sizeof(uint16_t)];

    modbus_set_float((float) new_value, &tab_reg[0]);
    // swap LSB and MSB
    uint16_t tmp1 = tab_reg[0];
    uint16_t tmp2 = tab_reg[1];
    tab_reg[0] = tmp2;
    tab_reg[1] = tmp1;

    if (command_delay) {
      log_message(debug_flag, "Sleeping command delay: %ldus", command_delay);
      usleep(command_delay);
    }

    int n = modbus_write_registers(ctx, address, nb, tab_reg);
    if (n != -1) {
        printf("New value %d for address 0x%X\n", new_value, address);
        if (restart == RESTART_TRUE) printf("You have to restart the meter for apply changes\n");
    } else {
        log_message(DEBUG_STDERR | DEBUG_SYSLOG, "error: (%d) %s, %d, %d", errno, modbus_strerror(errno), n);
        if (errno == EMBXILFUN) // Illegal function
            log_message(DEBUG_STDERR | DEBUG_SYSLOG, "Tip: is the meter in set mode?");
        exit_error(ctx);
    }
}

void changeConfigBCD(modbus_t *ctx, int address, int new_value, int restart, int nb)
{
    uint16_t tab_reg[nb * sizeof(uint16_t)];
    uint16_t u_new_value = int2bcd(new_value);
    tab_reg[0] = u_new_value;

    if (command_delay) {
      log_message(debug_flag, "Sleeping command delay: %ldus", command_delay);
      usleep(command_delay);
    }

    int n = modbus_write_registers(ctx, address, nb, tab_reg);
    if (n != -1) {
        printf("New value %d for address 0x%X\n", u_new_value, address);
        if (restart == RESTART_TRUE) printf("You have to restart the meter for apply changes\n");
    } else {
        log_message(DEBUG_STDERR | DEBUG_SYSLOG, "error: (%d) %s, %d, %d", errno, modbus_strerror(errno), n);
        if (errno == EMBXILFUN) // Illegal function
            log_message(DEBUG_STDERR | DEBUG_SYSLOG, "Tip: is the meter in set mode?");
        exit_error(ctx);
    }
}

/*--------------------------------------------------------------------------
    getIntLen
----------------------------------------------------------------------------*/
int getIntLen(long value){
  long l=!value;
  while(value) { l++; value/=10; }
  return l;
}

/*--------------------------------------------------------------------------
    getPIDcmd
----------------------------------------------------------------------------*/
void *getPIDcmd(long unsigned int PID)
{
    int fdcmd;
    char *COMMAND = NULL;
    size_t cmdLen = 0;
    size_t length;
    char buffer[1024];
    char cmdFilename[getIntLen(PID)+14+1];

    // Generate the name of the cmdline file for the process
    *cmdFilename = '\0';
    snprintf(cmdFilename,sizeof(cmdFilename),"/proc/%lu/cmdline",PID);
    
    // Read the contents of the file
    if ((fdcmd  = open(cmdFilename, O_RDONLY)) < 0) return NULL;
    if ((length = read(fdcmd, buffer, sizeof(buffer))) <= 0) {
        close(fdcmd); return NULL;
    }     
    close(fdcmd);
    
    // read does not NUL-terminate the buffer, so do it here
    buffer[length] = '\0';
    // Get 1st string (command)
    cmdLen=strlen(buffer)+1;
    if((COMMAND = getMemPtr(cmdLen)) != NULL ) {
        strncpy(COMMAND, buffer, cmdLen);
        COMMAND[cmdLen-1] = '\0';
    }

    return COMMAND;
}

int main(int argc, char* argv[])
{
    static int device_address[10] = {1};
    int idevices = -1;
    int ndevices = 1;
    
    
    int model          = MODEL_120;
    int new_address    = 0;
    int power_flag     = 0;
    int volt_flag      = 0;
    int current_flag   = 0;
    int pangle_flag    = 0;
    int freq_flag      = 0;
    int pf_flag        = 0;
    int apower_flag    = 0;
    int rapower_flag   = 0;
    int export_flag    = 0;
    int import_flag    = 0;
    int total_flag     = 0;
    int rexport_flag   = 0;
    int rimport_flag   = 0;
    int rtotal_flag    = 0;
    int new_baud_rate  = 0;
    int new_parity_stop= -1;
    int compact_flag   = 0;
    int time_disp_flag = 0;
    int rotation_time_flag = 0;
    int rotation_time  = 0; 
    int measurement_mode_flag = 0;
    int measurement_mode = 0; 
    int count_param    = 0;
    int num_retries    = 1;
#if LIBMODBUS_VERSION_MAJOR >= 3 && LIBMODBUS_VERSION_MINOR >= 1 && LIBMODBUS_VERSION_MICRO >= 2
    uint32_t resp_timeout = 2;
    uint32_t byte_timeout = -1;    
#else
    time_t resp_timeout = 2;
    time_t byte_timeout = -1;
#endif
    char *szttyDevice  = NULL;

    int c;
    int speed          = 0;
    int bits           = 0;
    int read_count     = 0;

    const char *EVEN_parity = "E";
    const char *NONE_parity = "N";
    const char *ODD_parity  = "O";
    char *c_parity     = NULL;
    
    int baud_rate      = 0;
    int stop_bits      = 0;
    char parity        = E_PARITY;
    
    programName        = argv[0];

    if (argc == 1) {
        usage(programName);
        exit(EXIT_FAILURE);
    }

    srand(getpid()^time(NULL));      // Init random numbers

    PID = getpid();
    getCmdLine();

    PPID = getppid();
    PARENTCOMMAND = getPIDcmd(PPID); 

    opterr = 0;

    while ((c = getopt (argc, argv, "a:Ab:BcCd:D:efgij:lmM:nN:opP:qr:R:s:S:tTvw:W:xy:z:12")) != -1) {
        switch (c)
        {
            case 'a':
                device_address[++idevices] = atoi(optarg);
                ndevices=idevices+1;
                if (!(0 < device_address[ndevices-1] && device_address[ndevices-1] <= 247)) {
                    fprintf (stderr, "%s: Address must be between 1 and 247.\n", programName);
                    exit(EXIT_FAILURE);
                }
                break;
            case 'v':
                volt_flag = 1;
                RTU_ReadRegistersRequests[VOLTAGE/2]=1;
                count_param++;
                break;
            case 'p':
                power_flag = 1;
                RTU_ReadRegistersRequests[POWER/2]=1;
                count_param++;
                break;
            case 'c':
                current_flag = 1;
                RTU_ReadRegistersRequests[CURRENT/2]=1;
                count_param++;
                break;
            case 'e':
                export_flag = 1;
                RTU_ReadRegistersRequests[EAENERGY/2]=1;                
                count_param++;
                break;
            case 'i':
                import_flag = 1;
                RTU_ReadRegistersRequests[IAENERGY/2]=1;                
                count_param++;
                break;
            case 't':
                total_flag = 1;
//                RTU_ReadRegistersRequests[TAENERGY/2]=1;                
                count_param++;
                break;
            case 'A':
                rimport_flag = 1;
                RTU_ReadRegistersRequests[IRAENERGY/2]=1;                
                count_param++;
                break;
            case 'B':
                rexport_flag = 1;
                RTU_ReadRegistersRequests[ERAENERGY/2]=1;                
                count_param++;
                break;
            case 'C':
                rtotal_flag = 1;
//                RTU_ReadRegistersRequests[TRENERGY/2]=1;                
                count_param++;
                break;
            case 'f':
                freq_flag = 1;
                RTU_ReadRegistersRequests[FREQUENCY/2]=1;
                count_param++;
                break;
            case 'g':
                pf_flag = 1;
                RTU_ReadRegistersRequests[PFACTOR/2]=1;
                count_param++;
                break;
            case 'l':
                apower_flag = 1;
                RTU_ReadRegistersRequests[APOWER/2]=1;
                count_param++;
                break;
            case 'n':
                rapower_flag = 1;
                RTU_ReadRegistersRequests[RAPOWER/2]=1;
                count_param++;
                break;
            case 'o':
                pangle_flag = 1;
                RTU_ReadRegistersRequests[PANGLE/2]=1;
                count_param++;
                break;
            case 'd':
                switch (*optarg) {
                    case '0':
                    case '1':
                    case '2':
                    case '3':
                        debug_flag = atoi(optarg) & DEBUG_STDERR;
                        debug_mask = atoi(optarg);
                        break;
                    default:
                         fprintf (stderr, "%s: Debug value must be one of 0,1,2,3.\n", programName);
                         exit(EXIT_FAILURE);
                }
                break;
            case 'x':
                trace_flag = 1;
                break;
            case 'b':
                speed = atoi(optarg);
                if (speed == 1200 || speed == 2400 || speed == 4800 || speed == 9600) {
                    baud_rate = speed;
                } else {
                    fprintf (stderr, "%s: Baud Rate must be one of 1200, 2400, 4800, 9600\n", programName);
                    exit(EXIT_FAILURE);
                }
                break;
            case 'P':
                c_parity = strdup(optarg);
                if (strcmp(c_parity,EVEN_parity) == 0) {
                    parity = E_PARITY;
                } else if (strcmp(c_parity,NONE_parity) == 0) {
                    parity = N_PARITY;
                } else if (strcmp(c_parity,ODD_parity) == 0) {
                    parity = O_PARITY;
                } else {
                    fprintf (stderr, "%s: Parity must be one of E, N, O\n", programName);
                    exit(EXIT_FAILURE);
                }
                free(c_parity);
                break;
            case 'S':
                bits = atoi(optarg);
                if (bits == 1 || bits == 2) {
                    stop_bits = bits;
                } else {
                    fprintf (stderr, "%s: Stop bits can be one of 1, 2\n", programName);
                    exit(EXIT_FAILURE);
                }
                break;
            case 'r':
                speed = atoi(optarg);
                switch (speed) {
                    case 1200:
                        new_baud_rate = BR1200;
                        break;
                    case 2400:
                        new_baud_rate = BR2400;
                        break;
                    case 4800:
                        new_baud_rate = BR4800;
                        break;
                    case 9600:
                        new_baud_rate = BR9600;
                        break;
                    default:
                        fprintf (stderr, "%s: Baud Rate must be one of 1200, 2400, 4800, 9600\n", programName);
                        exit(EXIT_FAILURE);
                }
                break;
            case 'N':
                new_parity_stop = atoi(optarg);
                if (!(0 <= new_parity_stop && new_parity_stop <= 3)) {
                    fprintf (stderr, "%s: New parity/stop (%d) out of range, 0-3.\n", programName, new_parity_stop);
                    exit(EXIT_FAILURE);
                }
                break;
            case 's':
                new_address = atoi(optarg);
                if (!(0 < new_address && new_address <= 247)) {
                    fprintf (stderr, "%s: New address (%d) out of range, 1-247.\n", programName, new_address);
                    exit(EXIT_FAILURE);
                }
                break;
            case 'R':
                rotation_time_flag = 1;
                rotation_time = atoi(optarg);

                if (model == MODEL_120 && !(0 <= rotation_time && rotation_time <= 30)) {
                    fprintf (stderr, "%s: New rotation time (%d) out of range, 0-30.\n", programName, rotation_time);
                    exit(EXIT_FAILURE);
                } else if (model == MODEL_220 && !(0 <= rotation_time && rotation_time <= 9999)) {
                    fprintf (stderr, "%s: SDM220 display time composite parameter (%d) out of range, 0-9999.\n", programName, rotation_time);
                    exit(EXIT_FAILURE);
                }
                break;
            case 'M':
                measurement_mode_flag = 1;
                measurement_mode = atoi(optarg);
                if (!(1 <= measurement_mode && measurement_mode <= 3)) {
                    fprintf (stderr, "%s: New measurement mode (%d) out of range, 1-3.\n", programName, rotation_time);
                    exit(EXIT_FAILURE);
                }
                break;
                break;
            case '1':
                model = MODEL_120;
                break;
            case '2':
                model = MODEL_220;
                break;
            case 'm':
                metern_flag = 1;
                break;
            case 'q':
                compact_flag = 1;
                break;
            case 'z':
                num_retries = atoi(optarg);
                if (!(0 < num_retries && num_retries <= MAX_RETRIES)) {
                    fprintf (stderr, "%s: num_retries (%d) out of range, 1-%d.\n", programName, num_retries, MAX_RETRIES);
                    exit(EXIT_FAILURE);
                }
                break;
            case 'j':
                resp_timeout = atoi(optarg);
                if (resp_timeout < 1 || resp_timeout > 500) {
                    fprintf(stderr, "%s: -j Response timeout (%lu) out of range, 0-500.\n",programName,(long unsigned)resp_timeout);
                    exit(EXIT_FAILURE);
                }
                break;
            case 'y':
                byte_timeout = atoi(optarg);
                if (byte_timeout < 1 || byte_timeout > 500) {
                    fprintf(stderr, "%s: -y Byte timeout (%lu) out of range, 1-500.\n",programName,(long unsigned)byte_timeout);
                    exit(EXIT_FAILURE);
                }
                break;
            case 'w':
                yLockWait = atoi(optarg);
                if (yLockWait < 1 || yLockWait > 30) {
                    fprintf(stderr, "%s: -w Lock Wait seconds (%d) out of range, 1-30.\n",programName,yLockWait);
                    exit(EXIT_FAILURE);
                }
                break;
            case 'W':
                settle_time = atoi(optarg);
                break;
            case 'D':
                command_delay = atoi(optarg);
                break;
            case 'T':
                time_disp_flag = 1;
                count_param++;
                break;
            case '?':
                if (isprint (optopt)) {
                    fprintf (stderr, "%s: Unknown option `-%c'.\n", programName, optopt);
                    usage(programName);
                    exit(EXIT_FAILURE);
                } else {
                    fprintf (stderr,"%s: Unknown option character `\\x%x'.\n",programName, optopt);
                    usage(programName);
                    exit(EXIT_FAILURE);
                }
            default:
                fprintf (stderr, "%s: Unknown option `-%c'.\n", programName, optopt);
                usage(programName);
                exit(EXIT_FAILURE);
        }
    }

    log_message(debug_flag, "cmdline=\"%s\"", cmdline);
        
    if (optind < argc) {               /* get serial device name */
        szttyDevice = argv[optind];
     } else {
        log_message(debug_flag, "optind = %d, argc = %d", optind, argc);
        usage(programName);
        fprintf(stderr, "%s: No serial device specified\n", programName);
        exit(EXIT_FAILURE);
    }

    if (compact_flag == 1 && metern_flag == 1) {
        fprintf(stderr, "%s: Parameter -m and -q are mutually exclusive\n", programName);
        usage(programName);
        exit(EXIT_FAILURE);
    }

    LockSer(szttyDevice, PID, debug_flag);

    modbus_t *ctx;
    
    // Baud rate
    if (baud_rate == 0) baud_rate = DEFAULT_RATE;

    // Response timeout
    resp_timeout *= 100000;    
    log_message(debug_flag, "resp_timeout=%ldus", resp_timeout);
    
    // Byte timeout
    if (byte_timeout != -1) {
        byte_timeout *= 1000;    
        log_message(debug_flag, "byte_timeout=%ldus", byte_timeout);
    }
    
    // Command delay
    if (command_delay == -1) {
        command_delay = 0;      // default = no command delay
    } else { 
        command_delay *= 1000;        
        log_message(debug_flag, "command_delay=%ldus", command_delay);
    }

    // Settle time delay
    if (settle_time == -1)
        //settle_time = 20000;    // default = 20ms
        settle_time = 0;    // default = 0ms
    else {
        settle_time *= 1000;
        log_message(debug_flag, "settle_time=%ldus", settle_time);
    }

    if (stop_bits == 0) {
        if (parity != N_PARITY)
            stop_bits=1;     // Default if parity != N
        else
            stop_bits=2;     // Default if parity == N        
    }

    //--- Modbus Setup start ---
    
    ctx = modbus_new_rtu(szttyDevice, baud_rate, parity, 8, stop_bits);
    if (ctx == NULL) {
        log_message(debug_flag | DEBUG_SYSLOG, "Unable to create the libmodbus context\n");
        ClrSerLock(PID);
        exit(EXIT_FAILURE);
    } else {
        log_message(debug_flag, "Libmodbus context open (%d%s%d)",
                baud_rate,
                (parity == E_PARITY) ? EVEN_parity :
                (parity == N_PARITY) ? NONE_parity :
                ODD_parity,
                stop_bits);
    }

#if LIBMODBUS_VERSION_MAJOR >= 3 && LIBMODBUS_VERSION_MINOR >= 1 && LIBMODBUS_VERSION_MICRO >= 2

    // Considering to get those values from command line
    if (byte_timeout == -1) {
        modbus_set_byte_timeout(ctx, -1, 0);
        log_message(debug_flag, "Byte timeout disabled.");
    } else {
        modbus_set_byte_timeout(ctx, 0, byte_timeout);
        log_message(debug_flag, "New byte timeout: %ds, %dus", 0, byte_timeout);
    }
    modbus_set_response_timeout(ctx, 0, resp_timeout);
    log_message(debug_flag, "New response timeout: %ds, %dus", 0, resp_timeout);

#else

    struct timeval timeout;

    if (byte_timeout == -1) {
        timeout.tv_sec = -1;
        timeout.tv_usec = 0;
        modbus_set_byte_timeout(ctx, &timeout);
        log_message(debug_flag, "Byte timeout disabled.");
    } else {
        timeout.tv_sec = 0;
        timeout.tv_usec = byte_timeout;
        modbus_set_byte_timeout(ctx, &timeout);
        log_message(debug_flag, "New byte timeout: %ds, %dus", timeout.tv_sec, timeout.tv_usec);
    }
    
    timeout.tv_sec = 0;
    timeout.tv_usec = resp_timeout;
    modbus_set_response_timeout(ctx, &timeout);
    log_message(debug_flag, "New response timeout: %ds, %dus", timeout.tv_sec, timeout.tv_usec);

#endif

    //modbus_set_error_recovery(ctx, MODBUS_ERROR_RECOVERY_LINK | MODBUS_ERROR_RECOVERY_PROTOCOL);
    //modbus_set_error_recovery(ctx, MODBUS_ERROR_RECOVERY_PROTOCOL);
    modbus_set_error_recovery(ctx, MODBUS_ERROR_RECOVERY_NONE);
    
    if (trace_flag == 1) {
        modbus_set_debug(ctx, 1);
    }

    if (modbus_connect(ctx) == -1) {
        log_message(DEBUG_STDERR | DEBUG_SYSLOG, "Connection failed: (%d) %s\n", errno, modbus_strerror(errno));
        modbus_free(ctx);
        ClrSerLock(PID);
        exit(EXIT_FAILURE);
    }
    
    for (idevices=0; idevices<ndevices; idevices++) {

        log_message(debug_flag, "Connecting to device id: %d", device_address[idevices]);
        if (settle_time) {
          // Wait for line settle
          log_message(debug_flag, "Sleeping %ldus for line settle...", settle_time);
          usleep(settle_time);
        }
            
        modbus_set_slave(ctx, device_address[idevices]);
    
        //log_message(debug_flag, "Flushed %d bytes", modbus_flush(ctx)); // Already flushed by connect 
    
        float voltage     = 0;
        float current     = 0;
        float power       = 0;
        float apower      = 0;
        float rapower     = 0;
        float pf          = 0;
        float pangle      = 0;
        float freq        = 0;
        float imp_energy  = 0;
        float exp_energy  = 0;
        float tot_energy  = 0;
        float impr_energy = 0;
        float expr_energy = 0;
        float totr_energy = 0;
        int   time_disp   = 0;
    
        if (new_address > 0 && new_baud_rate > 0) {
            log_message(DEBUG_STDERR, "Parameter -s and -r are mutually exclusive\n\n");
            usage(programName);
            exit_error(ctx);
        } else if ((new_address > 0 || new_baud_rate > 0) && new_parity_stop >= 0) {
            log_message(DEBUG_STDERR, "Parameter -s, -r and -N are mutually exclusive\n\n");
            usage(programName);
            exit_error(ctx);
        } else if (new_address > 0) {
    
            if (count_param > 0) {
                usage(programName);
                modbus_close(ctx);
                modbus_free(ctx);
                ClrSerLock(PID);
                exit(EXIT_FAILURE);
            } else {
                // change Address
                changeConfigFloat(ctx, DEVICE_ID, new_address, RESTART_FALSE, 2);
                modbus_close(ctx);
                modbus_free(ctx);
                ClrSerLock(PID);
                return 0;
            }
    
        } else if (new_baud_rate > 0) {
    
            if (count_param > 0) {
                usage(programName);
                modbus_close(ctx);
                modbus_free(ctx);
                ClrSerLock(PID);
                exit(EXIT_FAILURE);
            } else {
                // change Baud Rate
                changeConfigFloat(ctx, BAUD_RATE, new_baud_rate, RESTART_FALSE, 2);
                modbus_close(ctx);
                modbus_free(ctx);
                ClrSerLock(PID);
                return 0;
            }
    
        } else if (new_parity_stop >= 0) {
    
            if (count_param > 0) {
                usage(programName);
                modbus_close(ctx);
                modbus_free(ctx);
                ClrSerLock(PID);
                exit(EXIT_FAILURE);
            } else {
                // change Parity/Stop
                changeConfigFloat(ctx, NPARSTOP, new_parity_stop, RESTART_TRUE, 2);
                modbus_close(ctx);
                modbus_free(ctx);
                ClrSerLock(PID);
                return 0;
            }
    
        } else if (rotation_time_flag > 0) {
    
            if (count_param > 0) {
                usage(programName);
                modbus_close(ctx);
                modbus_free(ctx);
                ClrSerLock(PID);
                exit(EXIT_FAILURE);
            } else {
                // change Time Rotation
                changeConfigBCD(ctx, 
                                model == MODEL_120 ? TIME_DISP : TIME_DISP_220, 
                                rotation_time, RESTART_FALSE, 1);
                modbus_close(ctx);
                modbus_free(ctx);
                ClrSerLock(PID);
                return 0;
            }
    
        } else if (measurement_mode_flag > 0) {
    
            if (count_param > 0) {
                usage(programName);
                modbus_close(ctx);
                modbus_free(ctx);
                ClrSerLock(PID);
                exit(EXIT_FAILURE);
            } else {
                // change Measurement Mode
                changeConfigHex(ctx, TOT_MODE, measurement_mode, RESTART_FALSE);
                modbus_close(ctx);
                modbus_free(ctx);
                ClrSerLock(PID);
                return 0;
            }
    
        } else if (power_flag   == 0 &&
                   apower_flag  == 0 &&
                   rapower_flag == 0 &&
                   volt_flag    == 0 &&
                   current_flag == 0 &&
                   pf_flag      == 0 &&
                   pangle_flag  == 0 &&
                   freq_flag    == 0 &&
                   export_flag  == 0 &&
                   import_flag  == 0 &&
                   total_flag   == 0 &&
                   rexport_flag == 0 &&
                   rimport_flag == 0 &&
                   rtotal_flag  == 0 &&
                   time_disp_flag == 0
           ) {
           // if no parameter, retrieve all values
            power_flag   = 1;
            apower_flag  = 1;
            rapower_flag = 1;
            volt_flag    = 1;
            current_flag = 1;
            pangle_flag  = 1;
            freq_flag    = 1;
            pf_flag      = 1;
            export_flag  = 1;
            import_flag  = 1;
            total_flag   = 1;
            rexport_flag  = 1;
            rimport_flag  = 1;
            rtotal_flag   = 1;
            count_param  = power_flag + apower_flag + rapower_flag + volt_flag + 
                           current_flag + pangle_flag + freq_flag + pf_flag + 
                           export_flag + import_flag + total_flag +
                           rexport_flag + rimport_flag + rtotal_flag;
            RTU_ReadRegistersRequests[0]=1; RTU_ReadRegistersRequests[0x50/2-1]=1;
        }
    
        log_message(debug_flag, "readRegisters(ctx, 0, 0x50, buffer, requests, %d)", num_retries);
        readRegisters(ctx, 0, 0x50, RTU_ReadRegistersBuffer, RTU_ReadRegistersRequests, num_retries);
    
        if (volt_flag == 1) {
            voltage = getMeasureFloat(ctx, VOLTAGE, num_retries, 2);
            read_count++;
            if (metern_flag == 1) {
                printf("%d_V(%3.2f*V)\n", device_address[idevices], voltage);
            } else if (compact_flag == 1) {
                printf("%3.2f ", voltage);
            } else {
                printf("Voltage: %3.2f V \n",voltage);
            }
        }
    
        if (current_flag == 1) {
            current  = getMeasureFloat(ctx, CURRENT, num_retries, 2);
            read_count++;
            if (metern_flag == 1) {
                printf("%d_C(%3.2f*A)\n", device_address[idevices], current);
            } else if (compact_flag == 1) {
                printf("%3.2f ", current);
            } else {
                printf("Current: %3.2f A \n",current);
            }
        }
    
        if (power_flag == 1) {
            power = getMeasureFloat(ctx, POWER, num_retries, 2);
            read_count++;
            if (metern_flag == 1) {
                printf("%d_P(%3.2f*W)\n", device_address[idevices], power);
            } else if (compact_flag == 1) {
                printf("%3.2f ", power);
            } else {
                printf("Power: %3.2f W \n", power);
            }
        }
    
        if (apower_flag == 1) {
            apower = getMeasureFloat(ctx, APOWER, num_retries, 2);
            read_count++;
            if (metern_flag == 1) {
                printf("%d_VA(%3.2f*VA)\n", device_address[idevices], apower);
            } else if (compact_flag == 1) {
                printf("%3.2f ", apower);
            } else {
                printf("Active Apparent Power: %3.2f VA \n", apower);
            }
        }
    
        if (rapower_flag == 1) {
            rapower = getMeasureFloat(ctx, RAPOWER, num_retries, 2);
            read_count++;
            if (metern_flag == 1) {
                printf("%d_VAR(%3.2f*VAR)\n", device_address[idevices], rapower);
            } else if (compact_flag == 1) {
                printf("%3.2f ", rapower);
            } else {
                printf("Reactive Apparent Power: %3.2f VAR \n", rapower);
            }
        }
    
        if (pf_flag == 1) {
            pf = getMeasureFloat(ctx, PFACTOR, num_retries, 2);
            read_count++;
            if (metern_flag == 1) {
                printf("%d_PF(%3.2f*F)\n", device_address[idevices], pf);
            } else if (compact_flag == 1) {
                printf("%3.2f ", pf);
            } else {
                printf("Power Factor: %3.2f \n", pf);
            }
        }
    
        if (pangle_flag == 1) {
            pangle = getMeasureFloat(ctx, PANGLE, num_retries, 2);
            read_count++;
            if (metern_flag == 1) {
                printf("%d_PA(%3.2f*Dg)\n", device_address[idevices], pangle);
            } else if (compact_flag == 1) {
                printf("%3.2f ", pangle);
            } else {
                printf("Phase Angle: %3.2f Degree \n", pangle);
            }
        }
    
        if (freq_flag == 1) {
            freq = getMeasureFloat(ctx, FREQUENCY, num_retries, 2);
            read_count++;
            if (metern_flag == 1) {
                printf("%d_F(%3.2f*Hz)\n", device_address[idevices], freq);
            } else if (compact_flag == 1) {
                printf("%3.2f ", freq);
            } else {
                printf("Frequency: %3.2f Hz \n", freq);
            }
        }
    
        if (import_flag == 1) {
            imp_energy = getMeasureFloat(ctx, IAENERGY, num_retries, 2) * 1000;
            read_count++;
            if (metern_flag == 1) {
                printf("%d_IE(%d*Wh)\n", device_address[idevices], (int)imp_energy);
            } else if (compact_flag == 1) {
                printf("%d ", (int)imp_energy);
            } else {
                printf("Import Active Energy: %d Wh \n", (int)imp_energy);
            }
        }
    
        if (export_flag == 1) {
            exp_energy = getMeasureFloat(ctx, EAENERGY, num_retries, 2) * 1000;
            read_count++;
            if (metern_flag == 1) {
                printf("%d_EE(%d*Wh)\n", device_address[idevices], (int)exp_energy);
            } else if (compact_flag == 1) {
                printf("%d ", (int)exp_energy);
            } else {
                printf("Export Active Energy: %d Wh \n", (int)exp_energy);
            }
        }
    
        if (total_flag == 1) {
            tot_energy = getMeasureFloat(ctx, TAENERGY, num_retries, 2) * 1000;
            read_count++;
            if (metern_flag == 1) {
                printf("%d_TE(%d*Wh)\n", device_address[idevices], (int)tot_energy);
            } else if (compact_flag == 1) {
                printf("%d ", (int)tot_energy);
            } else {
                printf("Total Active Energy: %d Wh \n", (int)tot_energy);
            }
        }
    
        if (rimport_flag == 1) {
            impr_energy = getMeasureFloat(ctx, IRAENERGY, num_retries, 2) * 1000;
            read_count++;
            if (metern_flag == 1) {
                printf("%d_IRE(%d*VARh)\n", device_address[idevices], (int)impr_energy);
            } else if (compact_flag == 1) {
                printf("%d ", (int)impr_energy);
            } else {
                printf("Import Reactive Energy: %d VARh \n", (int)impr_energy);
            }
        }
    
        if (rexport_flag == 1) {
            expr_energy = getMeasureFloat(ctx, ERAENERGY, num_retries, 2) * 1000;
            read_count++;
            if (metern_flag == 1) {
                printf("%d_ERE(%d*VARh)\n", device_address[idevices], (int)expr_energy);
            } else if (compact_flag == 1) {
                printf("%d ", (int)expr_energy);
            } else {
                printf("Export Reactive Energy: %d VARh \n", (int)expr_energy);
            }
        }
    
        if (rtotal_flag == 1) {
            totr_energy = getMeasureFloat(ctx, TRENERGY, num_retries, 2) * 1000;
            read_count++;
            if (metern_flag == 1) {
                printf("%d_TRE(%d*VARh)\n", device_address[idevices], (int)totr_energy);
            } else if (compact_flag == 1) {
                printf("%d ", (int)totr_energy);
            } else {
                printf("Total Reactive Energy: %d VARh \n", (int)totr_energy);
            }
        }
    
        if (time_disp_flag == 1) {
            time_disp = getConfigBCD(ctx,
                                     model == MODEL_120 ? TIME_DISP : TIME_DISP_220,
                                     num_retries, 1);
            read_count++;
            if (compact_flag == 1) {
                printf("%d ", (int) time_disp);
            } else {
                printf("Display rotation time: %d\n", (int) time_disp);
            }
        }

    }

    log_message(debug_flag, "Total Modbus Time: %ldus", TotalModbusTime);

    if (read_count / ndevices == count_param) {
        // log_message(debug_flag, "Flushed %d bytes", modbus_flush(ctx));
        modbus_close(ctx);
        modbus_free(ctx);
        ClrSerLock(PID);
        free(devLCKfile);
        free(devLCKfileNew);
        free(PARENTCOMMAND);
        if (!metern_flag) printf("OK\n");
    } else {
        exit_error(ctx);
    }

    return 0;
}

#ifdef __cplusplus
}
#endif
