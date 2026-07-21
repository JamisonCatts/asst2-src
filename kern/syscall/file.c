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

    // indicate error until proven otherwise
    *retval = -1;

    if (result)
    {
        return result;
    }

    // kprintf("in sys_open() path is %s\n", kernel_path);

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

    // kprintf("in sys_open() new fd is %d\n", fd);

    *retval = fd;

    return 0;
}

int sys_write(int fd, userptr_t buf, size_t size, int32_t *retval)
{   
    // is this for std
	// result = vfs_open("con:", O_WRONLY, 0664, &wv);

    // Assume there will be an error
    // *ret_val = -1;
    if (fd < 0 || fd >= OPEN_MAX)
    {
        return EBADF;
    }

    int result;

    // kprintf("entered sys_write with fd: %d\n", fd);


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
    int how = this_file->flags & O_ACCMODE;
    if (!(how == O_WRONLY || how == O_RDWR))
    {
        lock_release(this_file->lock);
        kprintf("In sys_write() flags said couldn't write\n");
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
    // kprintf("in sys_read() fd is %d\n", fd);
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

    // kprintf("In sys_read() returning %d for fd %d\n", *retval, fd);

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
    // kprintf("In sys_close() closing fd %d\n", fd);
    if (this_file->ref_count == 0)
    {
        
        // kprintf("In sys_close() ref count is 0, destroying file %d\n", fd);
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

int sys_lseek(int fd, off_t offset, int whence, off_t *retval){

    *retval = -1;

    if (fd < 0 || fd >= OPEN_MAX)
    {
        return EBADF;
    }
    int result;

    // kprintf("----lseek() entered with fd: %d and offset %lld\n", fd, (long long)offset);

    spinlock_acquire(&curproc->p_lock);
    struct file *this_file = curproc->fd_table[fd];
    spinlock_release(&curproc->p_lock);

    if (this_file == NULL)
    {
        kprintf("in lseek() bad fd given\n");
        return EBADF;
    }

    lock_acquire(this_file->lock);

    if (!VOP_ISSEEKABLE(this_file->vn)){
        kprintf("lseek failed because not seekable\n");
        lock_release(this_file->lock);
        return ESPIPE;
    }

    switch(whence){
        case SEEK_SET:
            kprintf("----In lseek(). in SEEK_SET case\n");
            this_file->offset = offset;
            *retval = this_file->offset;
            break;
        case SEEK_CUR:
            this_file->offset+= offset;
            *retval = this_file->offset;
            break;
        case SEEK_END:
            ;
            struct stat this_file_stat;
            result = VOP_STAT(this_file->vn, &this_file_stat);
            if (result){
                return result;
            }
            kprintf("lseek() called with SEEK_END. file size: %jd\n", this_file_stat.st_size);
            this_file->offset = this_file_stat.st_size + offset;
            *retval = this_file->offset;
            break;
        default:
            kprintf("----In lseek() bad whence given\n");
            return EINVAL;
            break;
    }
    lock_release(this_file->lock);


    return 0;
}

int dup2(int old_fd, int new_fd, int32_t *retval){

    *retval = -1;
    if (old_fd < 0 || old_fd >= OPEN_MAX)
    {
        return EBADF;
    }

    if (new_fd < 0 || new_fd >= OPEN_MAX){
        return EBADF;
    }

    // Check for same fd given
    if (old_fd == new_fd) {
        *retval = new_fd;
        return 0;
    }



    spinlock_acquire(&curproc->p_lock);
    struct file *old_file = curproc->fd_table[old_fd];

    if (old_file == NULL){
        kprintf("in lseek() bad fd given\n");
        spinlock_release(&curproc->p_lock);
        return EBADF;
    }

    struct file *new_file = curproc->fd_table[new_fd];


    // Close file if there is a file
    if (new_file != NULL){
    lock_acquire(new_file->lock);
    new_file->ref_count--;
    // kprintf("In sys_dup2() closing old fd %d\n", old_fd);
    if (new_file->ref_count == 0)
    {
        
        // kprintf("In sys_close() ref count is 0, destroying file %d\n", fd);
        struct vnode *vn = new_file->vn;
        
        lock_release(new_file->lock);
        // destroy lock because not needed
        lock_destroy(new_file->lock);
        vfs_close(vn);
        
        kfree(new_file);
    }
    else
    {
        lock_release(new_file->lock);
    }

    // New_fd file dealt with now
    curproc->fd_table[new_fd] = old_file;

    // increase ref count and we're good
    lock_acquire(old_file->lock);
    old_file->ref_count++;
    lock_release(old_file->lock);
    }

    spinlock_release(&curproc->p_lock);

    *retval = new_fd;

    

    return 0;
}