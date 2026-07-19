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
#include <spinlock.h>
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

// This uses the userptr and not the kernel buf idk
void uio_init(struct iovec *iov, struct uio *u, userptr_t buf, size_t len, off_t offset, enum uio_rw rw)
{
    iov->iov_ubase = buf;
    iov->iov_len = len;
    u->uio_iov = iov;
    u->uio_iovcnt = 1;
    u->uio_offset = offset;
    u->uio_resid = len;
    u->uio_segflg = UIO_USERSPACE;
    u->uio_rw = rw;
    u->uio_space = proc_getas();
}

int sys_open(userptr_t path, int flags, mode_t mode, int32_t *retval)
{

    int result;
    char kernel_path[PATH_MAX];
    struct vnode *vn;
    size_t got;
    result = copyinstr(path, kernel_path, PATH_MAX, &got);

    if (result)
    {
        return result;
    }

    kprintf("in sys_open() path is %s\n", kernel_path);

    result = vfs_open(kernel_path, flags, mode, &vn);

    if (result)
    {
        return result;
    }

    struct file *new_file = kmalloc(sizeof(struct file));

    if (new_file == NULL)
    {
        vfs_close(vn);
        return ENOMEM;
    }

    init_file(new_file, vn, flags, kernel_path);

    // For when index found
    // Default is negative??
    int fd = -1;

    // acquire lock for current process
    spinlock_acquire(&curproc->p_lock);
    int i;
    for (i = 3; i < OPEN_MAX; i++)
    {
        if (curproc->fd_table[i] == NULL)
        {
            curproc->fd_table[i] = new_file;
            fd = i;
            break;
        }
    }
    spinlock_release(&curproc->p_lock);

    // If can't open any more files
    if (i == OPEN_MAX)
    {
        kfree(new_file);
        vfs_close(vn);
        return ENOMEM;
    }

    kprintf("in sys_open() new fd is %d\n", fd);

    *retval = fd;

    return 0;
}

int sys_write(int fd, userptr_t buf, size_t size, int32_t *retval)
{

    // Assume there will be an error
    // *ret_val = -1;
    if (fd < 0 || fd >= OPEN_MAX)
    {
        return EBADF;
    }

    int result;



    // Later deal with stdard FDs

    spinlock_acquire(&curproc->p_lock);
    struct file *this_file = curproc->fd_table[fd];
    spinlock_release(&curproc->p_lock);

    if (this_file == NULL)
    {
        kprintf("in sys_write() bad fd given\n");
        return EBADF;
    }
    lock_acquire(this_file->lock);
    // Check if has write permission
    if ((this_file->flags & O_WRONLY) == 0 &&
        (this_file->flags & O_RDWR) == 0)
    {
        lock_release(this_file->lock);
        return EBADF;
    }
    struct uio u;
    struct iovec iov;
    uio_init(&iov, &u, buf, size, this_file->offset, UIO_WRITE);
    result = VOP_WRITE(this_file->vn, &u);
    lock_release(this_file->lock);


    if (result)
    {
        return result;
    }
    this_file->offset = u.uio_offset;

    *retval = size - u.uio_resid;
    return 0;
}

int sys_read(int fd, userptr_t buf, size_t size, int32_t *retval)
{

    if (fd < 0 || fd >= OPEN_MAX)
    {
        return EBADF;
    }
    kprintf("in sys_read() fd is %d\n", fd);
    int result;

    // spinlock only to get file
    spinlock_acquire(&curproc->p_lock);
    struct file *this_file = curproc->fd_table[fd];
    spinlock_release(&curproc->p_lock);

    if (this_file == NULL)
    {
        kprintf("in sys_read() bad fd given\n");
        return EBADF;
    }

    lock_acquire(this_file->lock);
    if ((this_file->flags & O_ACCMODE) == O_WRONLY)
    {
        lock_release(this_file->lock);
        kprintf("in sys_read() bad flag\n");
        return EBADF;
    }


    struct vnode *vn = this_file->vn;
    struct uio u;
    struct iovec iov;
    uio_init(&iov, &u, buf, size, this_file->offset, UIO_READ);
    result = VOP_READ(vn, &u);

    if (result)
    {
        lock_release(this_file->lock);
        kprintf("In sys_read() VOP_READ didn't work\n");
        return result;
    }
    this_file->offset = u.uio_resid;
    lock_release(this_file->lock);


    *retval = size - u.uio_resid;

    kprintf("In sys_read() returning %d for fd %d", *retval, fd);

    return 0;
}

int sys_close(int fd)
{

    if (fd < 0 || fd >= OPEN_MAX)
    {
        return EBADF;
    }


    spinlock_acquire(&curproc->p_lock);
    struct file *this_file = curproc->fd_table[fd];
    spinlock_release(&curproc->p_lock);

    if (this_file == NULL)
    {
        kprintf("bad fd given\n");
        return EBADF;
    }

    // remove reference to file while holding onto file
    curproc->fd_table[fd] = NULL;
    // release process now

    lock_acquire(this_file->lock);
    this_file->ref_count--;
    if (this_file->ref_count == 0)
    {

        struct vnode *vn = this_file->vn;

        lock_release(this_file->lock);
        // destroy lock because not needed
        lock_destroy(this_file->lock);
        vfs_close(vn);

        kfree(this_file);
    }
    else
    {
        lock_release(this_file->lock);
    }

    return 0;
}

// TODO: destroy file in fd_table when refcount == 0
// int sys_close(){

// }