// -----------------------------------------------------------------------------------------------
// Copyright (c) 2026, Garhoogin
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without modification, are permitted
// provided that the following conditions are met:
// 
// 1. Redistributions of source code must retain the above copyright notice, this list of
//    conditions and the following disclaimer.
// 
// 2. Redistributions in binary form must reproduce the above copyright notice, this list of
//    conditions and the following disclaimer in the documentation and/or other materials provided
//    with the distribution.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
// IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
// AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
// OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
// -----------------------------------------------------------------------------------------------
#pragma once

#include <stdint.h>
#include <wchar.h>


#define ISD_SERIAL_SIZE   10  // size of the serial number in bytes


#ifdef ISD_DLL
#define ISD_LIB __declspec(dllexport)
#else // ISD_DLL
#define ISD_LIB __declspec(dllimport)
#endif


// -----------------------------------------------------------------------------------------------
// Name: enum IsdStatus
//
// Indicates the last error code on the device.
//
// Boolean values:
//   ISD_STATUS_OK               No error occurred.
//   ISD_STATUS_IRREGULAR        An error occurred that results in the library in an unusable
//                               state. The library should be terminated.
//   ISD_STATUS_BUSY             Exclusive access to the device could not be acquired. The
//                               device may be being used by another process. Try again or use a
//                               lesser access.
//   ISD_STATUS_NOMEM            The operation failed because of insufficient memory on the host
//                               device. This error is non-fatal.
//   ISD_STATUS_COMM_CLOSE       The operation failed because the communication with the device
//                               was closed. No further communication can be successful and the
//                               device should be closed.
//   ISD_STATUS_MON_TIMEOUT      The operation failed because the communication with the monitor
//                               program timed out. Ensure that the monitor program is running.
//                               Otherwise, a hardware reset may be required.
//   ISD_STATUS_FAILED           The operation failed on the NEC executing a request. 
// -----------------------------------------------------------------------------------------------
typedef enum IsdStatus_ {
	ISD_STATUS_OK,             // No error.
	ISD_STATUS_IRREGULAR,      // The state is irregular. The library should be terminated.
	ISD_STATUS_BUSY,           // Exclusive access could not be acquired. Try again or use a lesser access.
	ISD_STATUS_NOMEM,          // Insufficient memory to complete the operation. Recoverable error.
	ISD_STATUS_COMM_CLOSE,     // The communication is closed unexpectedly. Try reconnecting.
	ISD_STATUS_MON_TIMEOUT,    // The debug monitor was unresponsive. Should hardware reset.
	ISD_STATUS_FAILED,         // A command was not executed successfully. Recoverable error.
} IsdStatus;

// -----------------------------------------------------------------------------------------------
// Name: enum IsdProc
//
// The Nitro processor cores.
//
// Boolean values:
//   TS_PROC_MAIN                The DS ARM9 processor.
//   TS_PROC_SUB                 The DS ARM7 processor.
// -----------------------------------------------------------------------------------------------
typedef enum IsdProc_ {
	TS_PROC_MAIN,              // The ARM9 processor
	TS_PROC_SUB                // The ARM7 processor
} IsdProc;

// -----------------------------------------------------------------------------------------------
// Name: enum IsdShareMode
//
// The sharing modes used to access the device.
//
// Boolean values:
//   ISD_SHARE_MODE_EXCLUSIVE    The device is opened with exclusive access for the entire
//                               duration it is open.
//   ISD_SHARE_MODE_SHARED       The access to the device is shared. Exclusive access is still
//                               requested on individual operations (and per user call to the
//                               IsdLockAcquire routine).
//   ISD_SHARE_MODE_PASSIVE      Exclusive access is never acquired. There are no guarantees of
//                               correct operation while other processes use the device.
// -----------------------------------------------------------------------------------------------
typedef enum IsdShareMode_ {
	ISD_SHARE_MODE_EXCLUSIVE,  // has exclusive ownership of the device at all times
	ISD_SHARE_MODE_SHARED,     // only acquires exclusive access for atomic operations
	ISD_SHARE_MODE_PASSIVE     // never acquires exclusive access
} IsdShareMode;

// -----------------------------------------------------------------------------------------------
// Name: struct IsdMonStatus
//
// The status of the monitor program.
//
// Values:
//   proc                        Which processor the monitor status is retrieved for. Use the
//                               IsdProc enum.
//   field1                      ?
//   cmdComplete                 Indicates that the command is complete when nonzero.
//   fiqEnter                    Indicates the FIQ program is running when nonzero.
//   monStatus                   ?
// -----------------------------------------------------------------------------------------------
typedef struct IsdMonStatus_ {
	unsigned char proc;             // indicates which processor (0=main, 1=sub)
	unsigned char field1;
	unsigned char cmdComplete;      // indicates the command is complete
	unsigned char fiqEnter;         // indicates the FIQ handler is ready
	uint32_t monStatus;             // ? (0=in process, 1=complete, 2=unknown?)
} IsdMonStatus;

typedef struct IsdDevice_ IsdDevice;



int IsdifInit(void);
int IsdifOpen(void);
int IsdifClose(void);
int IsdifStop(void);
int IsdifResume(void);
int IsdifReadMem(void *dest, uint32_t src, size_t size);
int IsdifWriteMem(uint32_t dest, const void *src, size_t size);
int IsdifGetMonStatus(IsdMonStatus *pStatus);

IsdStatus IsdGetLastError(void);
