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
    int ref_count;
    struct lock *lock;
};


void init_file(struct file *newfile, struct vnode *vn, int flags, char *path_name);


#endif /* _FILE_H_ */
