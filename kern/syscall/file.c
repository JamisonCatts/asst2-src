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

int sys_open(userptr_t path, int flags, mode_t mode, int_32_t *retval){

    char path_name[PATH_MAX];

    copyin(path, &path_name, PATH_MAX);

    kprintf("Has reached sys_open\n");
    kprintf("path is %s\n", path_name);
    kprintf("flags is %d\n", flags);
    kprintf("mode is %d\n", mode);


    return 0;
}