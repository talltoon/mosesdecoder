
#pragma once

#include <assert.h>
#include <stdint.h>

#ifdef WIN32

#include <io.h>
#include <stdio.h>
#include <sys/stat.h>

typedef int ssize_t;

#define _SC_PAGE_SIZE 1
#define MS_SYNC 1

int sysconf(int);
int msync(void*, int, int);
int ftruncate(int, int);

int lrint(int); 
size_t log10(size_t);
size_t ceil(size_t);
int getopt(int, char**, char*);

extern const char *optarg;
extern int optind;

struct timeval 
{
	float tv_sec, tv_usec;
};

struct rusage
{
	timeval ru_utime, ru_stime;
};

inline int getrusage(int, struct rusage*) { return 0; }
//extern int RUSAGE_SELF;

typedef int OFF_T;

int S_ISDIR(int);
int mkdtemp(const char*);
int rmdir(const char*);
int munmap(void *, int);
void *mmap(void*, int, int, int, int, OFF_T);

#define PROT_READ 1
#define PROT_WRITE 1
#define MAP_FAILED (void*) 0x1
#define MAP_SHARED 1
#define MAP_ANON 1
#define MAP_PRIVATE 1
#define S_IRUSR 1
#define S_IROTH 1
#define S_IRGRP 1

int write(int, const void *, int);
#define S_IRUSR 1
#define S_IWUSR 1
int S_ISREG(int);

const char *strerror_r(int, const char *buf, int);
float strtof(const char *begin, char **end);
FILE *popen(const char*, const char*);
int pclose(FILE *);

#define dup(x) _dup(x)

#else // assume UNIX OS

#include <inttypes.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

typedef off_t OFF_T;

#endif

#ifdef __GNUC__
#define UTIL_FUNC_NAME __PRETTY_FUNCTION__
#else
#ifdef _WIN32
#define UTIL_FUNC_NAME __FUNCTION__
#else
#define UTIL_FUNC_NAME NULL
#endif
#endif

/* Bit-level packing routines */
#ifdef __APPLE__
	#include <architecture/byte_order.h>
#elif __linux__
	#include <endian.h>
#elif WIN32
	// nothing
#else
	#include <arpa/nameser_compat.h>
#endif 

