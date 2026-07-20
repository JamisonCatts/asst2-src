/*
 * Declarations for file handle and file table management.
 */

#ifndef _FILE_H_
#define _FILE_H_

/*
 * Contains some file-related maximum length constants
 */
#include <limits.h>
#include <uio.h>


/*
 * Put your function declarations and data types here ...
 */

 // Used in fd_table in each process
struct file {
    struct vnode *vn;
    off_t offset;
    int flags;
    int ref_count;
    struct lock *lock;
};

void uio_init(struct iovec *iov, struct uio *u, userptr_t buf, size_t len, off_t offset, enum uio_rw rw);
void init_file(struct file *new_file, struct vnode *vn, int flags, char *path_name);



#endif /* _FILE_H_ */
