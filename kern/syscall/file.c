#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <kern/limits.h>
#include <kern/stat.h>
#include <kern/seek.h>
#include <lib.h>
#include <uio.h>
#include <proc.h>
#include <current.h>
#include <synch.h>
#include <vfs.h>
#include <vnode.h>
#include <file.h>
#include <syscall.h>
#include <copyinout.h>

/*
 * Add your file-related functions here ...
 */

void init_file(struct file *new_file, struct vnode *vn, int flags, char *path_name)
{
    new_file->vn = vn;
    new_file->offset = 0;
    new_file->flags = flags;
    new_file->ref_count = 1;
    new_file->lock = lock_create(path_name);
}

int sys_open(userptr_t path, int flags, mode_t mode, int32_t *retval)
{

    int result;
    char kernel_path[PATH_MAX];
    struct vnode *vn;
    size_t got;
    result = copyinstr(path, &kernel_path, PATH_MAX, &got);

    if (result)
    {
        return result;
    }

    result = vfs_open(kernel_path, flags, mode, &vn);

    if (result)
    {
        return result;
    }

    new_file = kmalloc(sizeof(struct file));

    if (new_file == NULL)
    {
        vfs_close(vn);
        return ENOMEM;
    }

    init_file(new_file, vn, flags, kernel_path);

        // For when index found
        int fd;

    // acquire lock for current process
    lock_acquire(curproc->p_lock);

    for (int i = 3; i < OPEN_MAX; i++)
    {
        if (curproc->fd_table[i] == NULL)
        {
            curr_proc->fd_table[i] = new_file;
            fd = i;
            break;
        }
    }
    lock_release(curproc->p_lock);

    // If can't open any more files
    if (i == OPEN_MAX)
    {
        kfree(new_file);
        vfs_close(vn);
        return ENOMEM;
    }

    *retval = fd;

    return 0;
}

int sys_write(int fd, userptr_t buf, size_t size)
{

    int result;

    char kernel_buf[size];

    result = copyin(buf, &kernel_buf, size);

    if (result)
    {
        return result;
    }

    (void)fd;

    return 0;
}