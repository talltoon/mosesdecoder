
#include <stdlib.h>
#include "util/portability.hh"

#ifdef WIN32

int RUSAGE_SELF = 0;

int sysconf(int) { return 0; }
int msync(void*, int, int) { return 0; }
int ftruncate(int, int) { return 0; }
int munmap(void *, int) { return 0; }
void *mmap(void*, int, int, int, int, OFF_T) { return 0; }
int write(int, const void *, int) {return 0; }

//FILE *popen(const char*, const char*) { return 0; }
//int pclose(FILE *) { return 0; }
//int lrint(int)  { return 0;} 

// to be implemented by boost
int S_ISDIR(int) { return 0; }
int S_ISREG(int) { return 0; }
int mkdtemp(const char*) { return 0; }

// done
float strtof(const char *begin, char **end) 
{ 
	double ret = strtod(begin, end);
	return (float) ret; 
}

void d()
{
	//log10(10);
	//ceil(10);
}

#endif 


