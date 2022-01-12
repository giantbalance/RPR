# posetrace

posetrace is a tool that traces virtual memory addresses referenced by
a given program.
It additionally records the number of total instructions executed and
the memory allocations functions in below:

1. malloc()
1. calloc()
1. realloc()
1. free()

The arguments and the return value of the functions are recorded.
The name of the directory is misleading, so please note that the name of the
tool is `posetrace`, not `pin` in fact.

## To build
```
$ cd pin-3.11-97998-g7ecce2dac-gcc-linux/source/tools/posetrace
$ make
$ cd -
$ ./install.sh
```


## How to use
```
$ posetrace <command>
```
For example,
```
$ posetrace ls
```
or,
```
$ posetrace ./a.out arg1 arg2
```
You can find the trace output with the name of `posetrace.out`.
What you need is only the posetrace and the binary of a program.


## Specification 
### Trace format
posetrace records the choronological sequence of referenced addresses and
the memory allocation functions.
The number of the instructions executed is recorded at the tail of the trace.
Following is the format of each information in the trace:

Information | Format | Prefix
----------- | --------- | ------
Memory address | address (unsigned long, 8B) + ref_size (signed int, 4B) | 0x0
malloc() | return_val (unsigned long, 8B) + alloc_size (unsigned long, 8B) | 0x8
calloc() | return_val (unsigned long, 8B) + nmemb (unsigned long, 8B) + size (unsigned long, 8B) | 0x9
realloc() | return_val (unsigned long, 8B) + ptr (unsigned long, 8B) + size (unsigned long, 8B)| 0xa
free() | return_val (unsigned long, 8B) | 0xb
\# of instructions | nr_insts (unsigned long, 8B) | 0xf

The prefix overrides 4 bits at the head of each data.

### Overhead
posetrace uses dynamic instrumentation to make the program automatically record
the information, thus it slows down a program by 100x ~ 1,000x.
