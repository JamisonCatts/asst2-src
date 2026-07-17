/*
 * Declarations for file handle and file table management.
 */

#ifndef _FILE_H_
#define _FILE_H_

/*
 * Contains some file-related maximum length constants
 */
#include <limits.h>


/*
 * Put your function declarations and data types here ...
 */

 // Used in fd_table in each process
struct file {
    struct vnode *vn;
    off_t offset;
    int flags;
    int refcount;
    struct lock *lock;
};

int sys_open(userptr_t path, int flags, mode_t mode, int32_t *retval);
int sys_write(int fd, userptr_t buf, size_t size);

#endif /* _FILE_H_ */
