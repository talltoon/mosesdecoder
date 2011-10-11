
#include "util/portability.hh"

#ifdef WIN32

const char *optarg = 0;
int optind = 0;
int RUSAGE_SELF = 0;

int sysconf(int) { return 0; }
int msync(void*, int, int) { return 0; }
int ftruncate(int, int) { return 0; }
int lrint(int)  { return 0;} 
size_t log10(size_t) { return 0; }
size_t ceil(size_t) { return 0; }
int getopt(int, char**, char*) { return 0; }
int S_ISDIR(int) { return 0; }
int mkdtemp(const char*) { return 0; }
int rmdir(const char*) { return 0; }
int munmap(void *, int) { return 0; }
void *mmap(void*, int, int, int, int, OFF_T) { return 0; }
int write(int, const void *, int) {return 0; }
int S_ISREG(int) { return 0; }
const char *strerror_r(int, const char *buf, int) { return ""; }
float strtof(const char *begin, char **end) { return 0.0f; }
FILE *popen(const char*, const char*) { return 0; }
int pclose(FILE *) { return 0; }

#endif 


