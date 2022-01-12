/* vim: set shiftwidth=4 softtabstop=4 tabstop=4: */
/*
 * Copyright 2002-2019 Intel Corporation.
 *
 * This software is provided to you as Sample Source Code as defined in the accompanying
 * End User License Agreement for the Intel(R) Software Development Products ("Agreement")
 * section 1.L.
 *
 * This software and the related documents are provided as is, with no express or implied
 * warranties, other than those that are expressly stated in the License.
 */

/*! @file
 *  This file contains an ISA-portable PIN tool for tracing memory accesses.
 */

#include "pin.H"
#include <iostream>
#include <fstream>
#include <iomanip>
using std::string;
using std::hex;
using std::ios;
using std::setw;
using std::cerr;
using std::dec;
using std::endl;

#define DEBUG 0

/* ===================================================================== */
/* Signitures for memory allocator functions
 *
 * 4 MSBs in addr are used for the signitures
 *
 * 0000: memory ref
 * 1000: malloc
 * 1001: calloc
 * 1010: realloc
 * 1011: free
 * 1100: mmap?
 * 1111: icount
 */
#define ClearSign(_addr)			((_addr) & ((0x1UL << 60) - 1))
#define SetSign(_addr, _sign)		(ClearSign(_addr) | ((_sign) << 60))

#define SignRef(_addr)				SetSign(_addr, 0x0UL);
#define SignMalloc(_addr)			SetSign(_addr, 0x8UL);
#define SignCalloc(_addr)			SetSign(_addr, 0x9UL);
#define SignRealloc(_addr)			SetSign(_addr, 0xaUL);
#define SignFree(_addr)				SetSign(_addr, 0xbUL);
#define SignMmap(_addr)				SetSign(_addr, 0xcUL);
#define SignIcount(_addr)			SetSign(_addr, 0xfUL);

/* ===================================================================== */
/* Names of malloc and free */
/* ===================================================================== */
#if defined(TARGET_MAC)
#define MALLOC "_malloc"
#define CALLOC "_calloc"
#define REALLOC "_realloc"
#define FREE "_free"
#else
#define MALLOC "malloc"
#define CALLOC "calloc"
#define REALLOC "realloc"
#define FREE "free"
#endif

/* ===================================================================== */
/* Global Variables */
/* ===================================================================== */

std::ofstream TraceFile;
#if DEBUG
std::ofstream DebugTraceFile;
#endif

/* ===================================================================== */
/* Commandline Switches */
/* ===================================================================== */

KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool",
		"o", "posetrace.out", "specify trace file name");
KNOB<BOOL> KnobValues(KNOB_MODE_WRITEONCE, "pintool",
		"values", "1", "Output memory values reads and written");

/* ===================================================================== */
/* Print Help Message                                                    */
/* ===================================================================== */

static INT32 Usage()
{
	cerr <<
		"This tool produces a memory address trace.\n"
		"For each (dynamic) instruction reading or writing to memory the the ip and ea are recorded\n"
		"\n";

	cerr << KNOB_BASE::StringKnobSummary();

	cerr << endl;

	return -1;
}

static VOID RecordMem(VOID * addr, INT32 size)
{
	ADDRINT address = SignRef((ADDRINT) addr);

#if DEBUG
	DebugTraceFile << addr << " " << size << endl;
#endif
	TraceFile.write((char *)&address, sizeof(ADDRINT));
	TraceFile.write((char *)&size, sizeof(INT32));
}

static VOID * WriteAddr;
static INT32 WriteSize;

static VOID RecordWriteAddrSize(VOID * addr, INT32 size)
{
	WriteAddr = addr;
	WriteSize = size;
}


static VOID RecordMemWrite(VOID)
{
	RecordMem(WriteAddr, WriteSize);
}

VOID Instruction(INS ins, VOID *v)
{

	// instruments loads using a predicated call, i.e.
	// the call happens iff the load will be actually executed

	if (INS_IsMemoryRead(ins) && INS_IsStandardMemop(ins))
	{
		INS_InsertPredicatedCall(
				ins, IPOINT_BEFORE, (AFUNPTR)RecordMem,
				IARG_MEMORYREAD_EA,
				IARG_MEMORYREAD_SIZE,
				IARG_END);
	}

	if (INS_HasMemoryRead2(ins) && INS_IsStandardMemop(ins))
	{
		INS_InsertPredicatedCall(
				ins, IPOINT_BEFORE, (AFUNPTR)RecordMem,
				IARG_MEMORYREAD2_EA,
				IARG_MEMORYREAD_SIZE,
				IARG_END);
	}

	// instruments stores using a predicated call, i.e.
	// the call happens iff the store will be actually executed
	if (INS_IsMemoryWrite(ins) && INS_IsStandardMemop(ins))
	{
		INS_InsertPredicatedCall(
				ins, IPOINT_BEFORE, (AFUNPTR)RecordWriteAddrSize,
				IARG_MEMORYWRITE_EA,
				IARG_MEMORYWRITE_SIZE,
				IARG_END);

		if (INS_IsValidForIpointAfter(ins))
		{
			INS_InsertCall(
					ins, IPOINT_AFTER, (AFUNPTR)RecordMemWrite,
					IARG_END);
		}
		if (INS_IsValidForIpointTakenBranch(ins))
		{
			INS_InsertCall(
					ins, IPOINT_TAKEN_BRANCH, (AFUNPTR)RecordMemWrite,
					IARG_END);
		}

	}
}

/* ===================================================================== */

static ADDRINT malloc_size;
static ADDRINT calloc_nmemb;
static ADDRINT calloc_size;
static ADDRINT realloc_ptr;
static ADDRINT realloc_size;

VOID MallocBefore(ADDRINT size)
{
	malloc_size = size;
}

/* ===================================================================== */

VOID MallocAfter(ADDRINT ret)
{
#if DEBUG
	DebugTraceFile << ret << " = malloc(" << malloc_size << ")" << endl;
#endif
	ret = SignMalloc(ret);
	TraceFile.write((char *)&ret, sizeof(ADDRINT));
	TraceFile.write((char *)&malloc_size, sizeof(ADDRINT));
}

/* ===================================================================== */

VOID CallocBefore(ADDRINT nmemb, ADDRINT size)
{
	calloc_nmemb = nmemb;
	calloc_size = size;
}

/* ===================================================================== */

VOID CallocAfter(ADDRINT ret)
{
#if DEBUG
	DebugTraceFile << ret << " = calloc(" << calloc_nmemb << ", "
		<< calloc_size << ")" << endl;
#endif
	ret = SignCalloc(ret);
	TraceFile.write((char *)&ret, sizeof(ADDRINT));
	TraceFile.write((char *)&calloc_nmemb, sizeof(ADDRINT));
	TraceFile.write((char *)&calloc_size, sizeof(ADDRINT));
}

/* ===================================================================== */

VOID ReallocBefore(ADDRINT ptr, ADDRINT size)
{
	realloc_ptr = ptr;
	realloc_size = size;
}

/* ===================================================================== */

VOID ReallocAfter(ADDRINT ret)
{
#if DEBUG
	DebugTraceFile << ret << " = realloc(" << realloc_ptr << ", "
		<< realloc_size << ")" << endl;
#endif
	ret = SignRealloc(ret);
	TraceFile.write((char *)&ret, sizeof(ADDRINT));
	TraceFile.write((char *)&realloc_ptr, sizeof(ADDRINT));
	TraceFile.write((char *)&realloc_size, sizeof(ADDRINT));
}

/* ===================================================================== */

VOID FreeBefore(ADDRINT ptr)
{
#if DEBUG
	DebugTraceFile << "free(" << ptr << ")" << endl;
#endif
	ptr = SignFree(ptr);
	TraceFile.write((char *)&ptr, sizeof(ADDRINT));
}

/* ===================================================================== */

VOID Image(IMG img, VOID *v)
{
	RTN mallocRtn = RTN_FindByName(img, MALLOC);
	RTN callocRtn = RTN_FindByName(img, CALLOC);
	RTN reallocRtn = RTN_FindByName(img, REALLOC);
	RTN freeRtn = RTN_FindByName(img, FREE);

	if (RTN_Valid(mallocRtn))
	{
		RTN_Open(mallocRtn);
		RTN_InsertCall(mallocRtn, IPOINT_BEFORE, (AFUNPTR)MallocBefore,
				IARG_FUNCARG_ENTRYPOINT_VALUE, 0, IARG_END);
		RTN_InsertCall(mallocRtn, IPOINT_AFTER, (AFUNPTR)MallocAfter,
				IARG_FUNCRET_EXITPOINT_VALUE, IARG_END);
		RTN_Close(mallocRtn);
	}

	if (RTN_Valid(callocRtn))
	{
		RTN_Open(callocRtn);
		RTN_InsertCall(callocRtn, IPOINT_BEFORE, (AFUNPTR)CallocBefore,
				IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
				IARG_FUNCARG_ENTRYPOINT_VALUE, 1, IARG_END);
		RTN_InsertCall(callocRtn, IPOINT_AFTER, (AFUNPTR)CallocAfter,
				IARG_FUNCRET_EXITPOINT_VALUE, IARG_END);
		RTN_Close(callocRtn);
	}

	if (RTN_Valid(reallocRtn))
	{
		RTN_Open(reallocRtn);
		RTN_InsertCall(reallocRtn, IPOINT_BEFORE, (AFUNPTR)ReallocBefore,
				IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
				IARG_FUNCARG_ENTRYPOINT_VALUE, 1, IARG_END);
		RTN_InsertCall(reallocRtn, IPOINT_AFTER, (AFUNPTR)ReallocAfter,
				IARG_FUNCRET_EXITPOINT_VALUE, IARG_END);
		RTN_Close(reallocRtn);
	}

	if (RTN_Valid(freeRtn))
	{
		RTN_Open(freeRtn);
		RTN_InsertCall(freeRtn, IPOINT_BEFORE, (AFUNPTR)FreeBefore,
				IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
				IARG_END);
		RTN_Close(freeRtn);
	}
}

UINT64 icount = 0;

VOID PIN_FAST_ANALYSIS_CALL docount(INT32 c)
{
	icount += c;
}

VOID Trace(TRACE trace, void *v) {
	for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl))
		BBL_InsertCall(bbl, IPOINT_ANYWHERE, (AFUNPTR)docount,
				IARG_FAST_ANALYSIS_CALL, IARG_UINT32, BBL_NumIns(bbl),
				IARG_END);
}

VOID RecordIcount(void)
{
	UINT64 count;

#if DEBUG
	DebugTraceFile << "icount: " << icount << endl;
#endif
	count = SignIcount(icount);
	TraceFile.write((char *)&count, sizeof(UINT64));
}

/* ===================================================================== */

VOID Fini(INT32 code, VOID *v)
{
	RecordIcount();
	TraceFile.close();
#if DEBUG
	DebugTraceFile.close();
#endif
}

/* ===================================================================== */
/* Main                                                                  */
/* ===================================================================== */

int main(int argc, char *argv[])
{
	PIN_InitSymbols();

	if( PIN_Init(argc,argv) )
	{
		return Usage();
	}

	TraceFile.open(KnobOutputFile.Value().c_str(), ios::out | ios::binary);
#if DEBUG
	DebugTraceFile.open("posetrace_debug.out");
	DebugTraceFile << hex;
	DebugTraceFile.setf(ios::showbase);
#endif

	TRACE_AddInstrumentFunction(Trace, 0);
	INS_AddInstrumentFunction(Instruction, 0);
	IMG_AddInstrumentFunction(Image, 0);
	PIN_AddFiniFunction(Fini, 0);

	// Never returns

	PIN_StartProgram();

	RecordMemWrite();
	RecordWriteAddrSize(0, 0);

	return 0;
}

/* ===================================================================== */
/* eof */
/* ===================================================================== */
