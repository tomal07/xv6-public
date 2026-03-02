#define NTHREADS      8                     // maximum number of threads each process group can have
#define NPROCGRPS     64                    // maximum number of process groups, each one can have up to NTHREADS threads
#define NPROC         (NTHREADS*NPROCGRPS)  // maximum number of process groups, each one can have up to NTHREADS threads
#define MIN_PID       1                     // The minimum pid
#define MAX_PID       NPROCGRPS             // The maximum pid
#define MIN_TID       1                     // The minumum tid
#define MAX_TID       NTHREADS              // The maximum tid
#define KSTACKSIZE 4096                     // size of per-process kernel stack
#define NCPU          8                     // maximum number of CPUs
#define NOFILE       16                     // open files per process
#define NFILE       100                     // open files per system
#define NINODE       50                     // maximum number of active i-nodes
#define NDEV         10                     // maximum major device number
#define ROOTDEV       1                     // device number of file system root disk
#define MAXARG       32                     // max exec arguments
#define MAXOPBLOCKS  10                     // max # of blocks any FS op writes
#define LOGSIZE       (MAXOPBLOCKS*3)       // max data blocks in on-disk log
#define NBUF          (MAXOPBLOCKS*3)       // size of disk block cache
#define FSSIZE       1000                   // size of file system in blocks
