/*************************************************************************
 * (C) Copyright 2016 Barcelona Supercomputing Center                    * 
 *                    Centro Nacional de Supercomputacion                *
 *                                                                       *
 * This file is part of the Echo Filesystem NG.                          *
 *                                                                       *
 * See AUTHORS file in the top level directory for information           *
 * regarding developers and contributors.                                *
 *                                                                       *
 * This library is free software; you can redistribute it and/or         *
 * modify it under the terms of the GNU Lesser General Public            *
 * License as published by the Free Software Foundation; either          *
 * version 3 of the License, or (at your option) any later version.      *
 *                                                                       *
 * Mercurium C/C++ source-to-source compiler is distributed in the hope  *
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the    *
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR       *
 * PURPOSE.  See the GNU Lesser General Public License for more          *
 * details.                                                              *
 *                                                                       *
 * You should have received a copy of the GNU Lesser General Public      *
 * License along with Mercurium C/C++ source-to-source compiler; if      *
 * not, write to the Free Software Foundation, Inc., 675 Mass Ave,       *
 * Cambridge, MA 02139, USA.                                             *
 *************************************************************************/

#define FUSE_USE_VERSION 28

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <fuse.h>

#ifdef HAVE_LIBULOCKMGR
extern "C" {
#include <ulockmgr.h>
}
#endif /* HAVE_LIBULOCKMGR */

#include <memory>
#include <cstring>
#include <iostream>
#include <cerrno>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>

#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif /* HAVE_SETXATTR */


#include "metadata/files.h"
#include "metadata/dirs.h"
#include "command-line.h"

/**********************************************************************************************************************/
/*   Filesytem operations
 *
 * Most of these should work very similarly to the well known UNIX file system operations.  A major exception is that
 * instead of returning an error in 'errno', the operation should return the negated error value (-errno) directly.
 *
 * All methods are optional, but some are essential for a useful filesystem (e.g. getattr).  Open, flush, release,
 * fsync, opendir, releasedir, fsyncdir, access, create, ftruncate, fgetattr, lock, init and destroy are special purpose
 * methods, without which a full featured filesystem can still be implemented.
 *
 * Almost all operations take a path which can be of any length.
 *
 * Changed in fuse 2.8.0 (regardless of API version) Previously, paths were limited to a length of PATH_MAX.
 *
 * See http://fuse.sourceforge.net/wiki/ for more information.  There is also a snapshot of the relevant wiki pages in
 * the doc/ folder.
 **********************************************************************************************************************/

/** Get file attributes. similar to stat() */
static int efsng_getattr(const char* pathname, struct stat* stbuf){

    if(lstat(pathname, stbuf) == -1){
        return -errno;
    }

    return 0;
}

/** Read the target of a symbolic link */
static int efsng_readlink(const char* pathname, char* buf, size_t bufsiz){

    int res = readlink(pathname, buf, bufsiz - 1);

    if(res == -1){
        return -errno;
    }

    /* the buffer needs to be filled with a null-terminated string */
    buf[res] = '\0';

    return 0;
}

/** Create a node for all non-directory, non-symlink nodes */
static int efsng_mknod(const char* pathname, mode_t mode, dev_t dev){

    if(mknod(pathname, mode, dev) == -1){
        return -errno;
    }

    return 0;
}

/** Create a directory */
static int efsng_mkdir(const char* pathname, mode_t mode){

    /* the mode argument may not have the specific bits set */
    if(!mode & S_IFDIR){
        mode |= S_IFDIR;
    }
    if(mkdir(pathname, mode) == -1){
        return -errno;
    }

    return 0;
}

/** Remove a file */
static int efsng_unlink(const char* pathname){

    if(unlink(pathname) == -1){
        return -errno;
    }

    return 0;
}

/** Remove a directory */
static int efsng_rmdir(const char* pathname){

    if(rmdir(pathname) == -1){
        return -errno;
    }

    return 0;
}

/** Create a symbolic link */
static int efsng_symlink(const char* oldpath, const char* newpath){

    if(symlink(oldpath, newpath) == -1){
        return -errno;
    }

    return 0;
}

/** Rename a file */
static int efsng_rename(const char* oldpath, const char* newpath){

    if(rename(oldpath, newpath) == -1){
        return -errno;
    }

    return 0;
}

/** Create a hard link to a file */
static int efsng_link(const char* oldpath, const char* newpath){

    if(link(oldpath, newpath) == -1){
        return -errno;
    }

    return 0;
}

/** Change the permissions bits of a file */
static int efsng_chmod(const char* pathname, mode_t mode){

    if(chmod(pathname, mode) == -1){
        return -errno;
    }

    return 0;
}

/** Change the owner and group of a file */
static int efsng_chown(const char* pathname, uid_t owner, gid_t group){

    if(lchown(pathname, owner, group) == -1){
        return -errno;
    }

    return 0;
}

/** Change the size of a file */
static int efsng_truncate(const char* pathname, off_t length){

    if(truncate(pathname, length) == -1){
        return -errno;
    }

    return 0;
}

/** 
 * File open operation
 *
 * No creation (O_CREAT, O_EXCL) and by default also no * truncation (O_TRUNC) flags will be passed to open(). If an
 * * application specifies O_TRUNC, fuse first calls truncate() * and then open(). Only if 'atomic_o_trunc' has been
 * * specified and kernel version is 2.6.24 or later, O_TRUNC is * passed on to open.
 *
 * Unless the 'default_permissions' mount option is given, open should check if the operation is permitted for the given
 * flags. Optionally open may also return an arbitrary filehandle in the fuse_file_info structure, which will be passed
 * to all file operations.
 */
static int efsng_open(const char* pathname, struct fuse_file_info* file_info){

    int fd = open(pathname, file_info->flags);

    if(fd == -1){
        return -errno;
    }

    int flags = 0;

    if(file_info->flags & O_RDONLY){
        flags |= O_RDONLY;
    }

    if(file_info->flags & O_WRONLY){
        flags |= O_WRONLY;
    }

    if(file_info->flags & O_RDWR){
        flags |= O_RDWR;
    }

    /* cache the inode, fd, and flags to reuse them later */
    struct stat st;
    fstat(fd, &st);

    // XXX this means that each "open()" creates a File record 
    // XXX WARNING: records ARE NOT protected by a mutex yet
    auto file_record = new efsng::File(st.st_ino, fd, flags);
    file_info->fh = (uint64_t) file_record;

    return 0;
}


/** 
 * Read data from an open file
 *
 * Read should return exactly the number of bytes requested except on EOF or error, otherwise the rest of the data will
 * be substituted with zeroes.   An exception to this is when the 'direct_io' mount option is specified, in which case
 * the return value of the read system call will reflect the return value of this operation.
 */
static int efsng_read(const char* pathname, char* buf, size_t count, off_t offset, struct fuse_file_info* file_info){

    (void) pathname;

    auto file_record = (efsng::File*) file_info->fh;

    int fd = file_record->get_fd();

    int res = pread(fd, buf, count, offset);

    if(res == -1){
        return -errno;
    }

    return res;
}

/** 
 * Write data to an open file
 *
 * Write should return exactly the number of bytes requested except on error.    An exception to this is when the
 * 'direct_io' mount option is specified (see read operation).
 */
static int efsng_write(const char* pathname, const char* buf, size_t count, off_t offset, 
                       struct fuse_file_info* file_info){

    (void) pathname;

    auto file_record = (efsng::File*) file_info->fh;

    int fd = file_record->get_fd();

    int res = pwrite(fd, buf, count, offset);

    if(res == -1){
        return -errno;
    }

    return res;
}

/** Get filesystem statistics */
static int efsng_statfs(const char* pathname, struct statvfs* buf){

    int res = statvfs(pathname, buf);

    if(res == -1){
        return -errno;
    }

    return 0;
}

/** 
 * Possibly flush cached data
 *
 * BIG NOTE: This is not equivalent to fsync().  It's not a
 * request to sync dirty data.
 *
 * Flush is called on each close() of a file descriptor.  So if a * filesystem wants to return write errors in close()
 * and the file has cached dirty data, this is a good place to write back data and return any errors.  Since many
 * applications ignore close() errors this is not always useful.
 *
 * NOTE: The flush() method may be called more than once for each open().  This happens if more than one file descriptor
 * refers to an opened file due to dup(), dup2() or fork() calls.   It is not possible to determine if a flush is final,
 * so each flush should be treated equally.  Multiple write-flush sequences are relatively rare, so this shouldn't be
 * a problem.
 *
 * Filesystems shouldn't assume that flush will always be called after some writes, or that if will be called at all.
 */

static int efsng_flush(const char* pathname, struct fuse_file_info* file_info){

    (void) pathname;

    auto file_record = (efsng::File*) file_info->fh;

    int fd = file_record->get_fd();

    /* This is called from every close on an open file, so call the
       close on the underlying filesystem.  But since flush may be
       called multiple times for an open file, this must not really
       close the file.  This is important if used on a network
       filesystem like NFS which flush the data/metadata on close() */
    int res = close(dup(fd));

    if (res == -1){
        return -errno;
    }

    return 0;
}

/** 
 * Release an open file
 *
 * Release is called when there are no more references to an open file: all file descriptors are closed and all
 * memory mappings are unmapped.
 *
 * For every open() call there will be exactly one release() call with the same flags and file descriptor. It is
 * possible to have a file opened more than once, in which case only the last release will mean, that no more
 * reads/writes will happen on the file.  The return value of release is ignored.
 */
static int efsng_release(const char* pathname, struct fuse_file_info* file_info){

    (void) pathname;

    auto file_record = (efsng::File*) file_info->fh;

    int fd = file_record->get_fd();

    if(close(fd) == -1){
        return -errno;
    }

    delete file_record;

    return 0;
}

/** 
 * Synchronize file contents
 *
 * If the datasync parameter is non-zero, then only the user data should be flushed, not the meta data.
 */
static int efsng_fsync(const char* pathname, int datasync, struct fuse_file_info* file_info){

    (void) pathname;
    (void) datasync;
    (void) file_info;

    auto file_record = (efsng::File*) file_info->fh;

    int fd = file_record->get_fd();

    int res;

#ifdef HAVE_FDATASYNC
    if(datasync){
        res = fdatasync(fd);
    }
    else{
        res = fsync(fd);
    }
#else
    res = fsync(fd);
#endif

    if(res == -1){
        return -errno;
    }

    return 0;
}

#ifdef HAVE_SETXATTR
/** Set extended attributes */
static int efsng_setxattr(const char* pathname, const char* name, const char* value, size_t size, int flags){

    int res = lsetxattr(pathname, name, value, size, flags);

    if(res == -1){
        return -errno;
    }

    return 0;
}

/** Get extended attributes */
static int efsng_getxattr(const char* pathname, const char* name, char* value, size_t size){

    int res = lgetxattr(pathname, name, value, size);

    if(res == -1){
        return -errno;
    }

    return 0;
}

/** List extended attributes */
static int efsng_listxattr(const char* pathname, char* listbuf, size_t size){

    int res = llistxattr(pathname, listbuf, size);

    if(res == -1){
        return -errno;
    }

    return 0;
}

/** Remove extended attributes */
static int efsng_removexattr(const char* pathname, const char* name){

    int res = lremovexattr(pathname, name);

    if(res == -1){
        return -errno;
    }

    return 0;
}
#endif /* HAVE_SETXATTR */

/** 
 * Open directory
 *
 * Unless the 'default_permissions' mount option is given, this method should check if opendir is permitted for this
 * directory. Optionally opendir may also return an arbitrary filehandle in the fuse_file_info structure, which will
 * be passed to readdir, closedir and fsyncdir.
 */
static int efsng_opendir(const char* pathname, struct fuse_file_info* file_info){

    DIR* dp = opendir(pathname);

    if(dp == NULL){
        return -errno;
    }

    auto dir_record = new efsng::Directory(dp, NULL, 0);
    file_info->fh = (uint64_t) dir_record;

    return 0;
}

/** 
 * Read directory
 *
 * The filesystem may choose between two modes of operation:
 *
 * 1) The readdir implementation ignores the offset parameter, and passes zero to the filler function's offset.  The
 * filler function will not return '1' (unless an error happens), so the whole directory is read in a single readdir
 * operation. This works just like the old getdir() method.
 *
 * 2) The readdir implementation keeps track of the offsets of the directory entries. It uses the offset parameter and
 * always passes non-zero offset to the filler function.  When the buffer is full (or an error happens) the filler
 * function will return '1'.
 */
static int efsng_readdir(const char* pathname, void* buf, fuse_fill_dir_t filler, off_t offset, 
                         struct fuse_file_info* file_info){

    (void) pathname;

    auto dir_record = (efsng::Directory*) file_info->fh;

    if(offset != dir_record->get_offset()){
        seekdir(dir_record->get_dirp(), offset);
        dir_record->set_entry(NULL);
        dir_record->set_offset(offset);
    }

    while(true){
        struct stat st;
        off_t next_offset;

        if(dir_record->get_entry() == NULL){
            struct dirent* dirent = readdir(dir_record->get_dirp());
            dir_record->set_entry(dirent);

            if(dirent == NULL){
                break;
            }
        }

        memset(&st, 0, sizeof(st));
        st.st_ino = dir_record->get_entry()->d_ino;
        st.st_mode = dir_record->get_entry()->d_type << 12;
        next_offset = telldir(dir_record->get_dirp());

        if(filler(buf, dir_record->get_entry()->d_name, &st, next_offset) != 0){
            break;
        }

        dir_record->set_entry(NULL);
        dir_record->set_offset(0);
    }

    return 0;
}

/** Release directory */
static int efsng_releasedir(const char* pathname, struct fuse_file_info* file_info){

    (void) pathname;

    auto dir_record = (efsng::Directory*) file_info->fh;

    if(closedir(dir_record->get_dirp()) == -1){
        return -errno;
    }

    delete dir_record;

    return 0;
}

/** 
 * Synchronize directory contents
 *
 * If the datasync parameter is non-zero, then only the user data should be flushed, not the metadata
 */
static int efsng_fsyncdir(const char* pathname, int, struct fuse_file_info* file_info){

    (void) pathname;
    (void) file_info;

    /* make sure we notice if this is ever used */
    assert(false);

    return 0;
}

/**
 * Initialize the filesystem
 *
 * The return value will passed in the private_data field of
 * fuse_context to all file operations and as a parameter to the
 * destroy() method.
 */
static void* efsng_init(struct fuse_conn_info *conn){

    (void) conn;

    return 0;
}

/**
 * Clean up the filesystem. Called on filesystem exit.
 */
static void efsng_destroy(void *){
}


/**
 * Check file access permissions
 *
 * This will be called for the access() system call.  If the 'default_permissions' mount option is given, this method is
 * not called.
 *
 * This method is not called under Linux kernel versions 2.4.x
 */
static int efsng_access(const char* pathname, int mode){

    if(access(pathname, mode) == -1){
        return -errno;
    }

    return 0;
}

/** 
 * Create and open a file
 *
 * If the file does not exist, first create it with the specified mode, and then open it.
 *
 * If this method is not implemented or under Linux kernel versions earlier than 2.6.15, the mknod() and open() methods
 * will be called instead.
 */
static int efsng_create(const char* pathname, mode_t mode, struct fuse_file_info* file_info){

    //XXX should we add O_CREAT|O_WRONLY|O_TRUNC ?
    int fd = open(pathname, file_info->flags, mode);

    if(fd == -1){
        return -errno;
    }

    int flags = 0;

    if(file_info->flags & O_RDONLY){
        flags |= O_RDONLY;
    }

    if(file_info->flags & O_WRONLY){
        flags |= O_WRONLY;
    }

    if(file_info->flags & O_RDWR){
        flags |= O_RDWR;
    }

    /* cache the inode, fd, and flags to reuse them later */
    struct stat st;
    fstat(fd, &st);

    // XXX this means that each "open()" creates a File record 
    // XXX WARNING: records ARE NOT protected by a mutex yet
    auto file_record = new efsng::File(st.st_ino, fd, flags);
    file_info->fh = (uint64_t) file_record;

    return 0;
}

/**
 * Change the size of an open file
 *
 * This method is called instead of the truncate() method if the truncation was invoked from an ftruncate() system call.
 *
 * If this method is not implemented or under Linux kernel versions earlier than 2.6.15, the truncate() method will be
 * called instead.
 */
static int efsng_ftruncate(const char* pathname, off_t length, struct fuse_file_info* file_info){

    (void) pathname;

    auto file_record = (efsng::File*) file_info->fh;
    int fd = file_record->get_fd();

    if(ftruncate(fd, length) == -1){
        return -errno;
    }

    return 0;
}

/**
 * Get attributes from an open file
 *
 * This method is called instead of the getattr() method if the file information is available.
 *
 * Currently this is only called after the create() method if that is implemented (see above).  Later it may be called
 * for invocations of fstat() too.
 */
static int efsng_fgetattr(const char* pathname, struct stat* stbuf, struct fuse_file_info* file_info){

    (void) pathname;

    auto file_record = (efsng::File*) file_info->fh;
    int fd = file_record->get_fd();

    if(fstat(fd, stbuf) == -1){
        return -errno;
    }

    return 0;
}

#ifdef HAVE_LIBULOCKMGR
/**
 * Perform POSIX file locking operation
 *
 * The cmd argument will be either F_GETLK, F_SETLK or F_SETLKW.
 *
 * For the meaning of fields in 'struct flock' see the man page for fcntl(2).  The l_whence field will always be set to
 * SEEK_SET.
 *
 * For checking lock ownership, the 'fuse_file_info->owner' argument must be used.
 *
 * For F_GETLK operation, the library will first check currently held locks, and if a conflicting lock is found it will
 * return information without calling this method.  This ensures, that for local locks the l_pid field is correctly
 * filled in.  The results may not be accurate in case of race conditions and in the presence of hard links, but it's
 * unlikely that an application would rely on accurate GETLK results in these cases.  If a conflicting lock is not
 * found, this method will be called, and the filesystem may fill out l_pid by a meaningful value, or it may leave this
 * field zero.
 *
 * For F_SETLK and F_SETLKW the l_pid field will be set to the pid of the process performing the locking operation.
 *
 * Note: if this method is not implemented, the kernel will still allow file locking to work locally.  Hence it is only
 * interesting for network filesystems and similar.
 */
static int efsng_lock(const char* pathname, struct fuse_file_info* file_info, int cmd, struct flock* flock){

    (void) pathname;

    auto file_record = (efsng::File*) file_info->fh;

    int fd = file_record->get_fd();

    int res = ulockmgr_op(fd, cmd, flock, &file_info->lock_owner, sizeof(file_info->lock_owner));

    /* ulockmgr_op returns 0 on success and -errno on error */
    return res;
}
#endif /* HAVE_LIBULOCKMGR */

#ifdef HAVE_UTIMENSAT
/**
 * Change the access and modification times of a file with nanosecond resolution
 *
 * This supersedes the old utime() interface.  New applications should use this.
 *
 * See the utimensat(2) man page for details.
 */
static int efsng_utimens(const char* pathname, const struct timespec tv[2]){

    (void) pathname;
    (void) tv;

    /* make sure we notice if this is ever used */
    assert(false);

    return 0;
}
#endif /* HAVE_UTIMENSAT */

/**
 * Map block index within file to block index within device
 *
 * Note: This makes sense only for block device backed filesystems mounted with the 'blkdev' option
 */
static int efsng_bmap(const char* pathname, size_t blocksize, uint64_t* idx){

    (void) pathname;
    (void) blocksize;
    (void) idx;

    /* make sure we notice if this is ever used */
    assert(false);

    return 0;
}

/**
 * Ioctl
 *
 * flags will have FUSE_IOCTL_COMPAT set for 32bit ioctls in 64bit environment.  The size and direction of data is
 * determined by _IOC_*() decoding of cmd.  For _IOC_NONE, data will be NULL, for _IOC_WRITE data is out area, for
 * _IOC_READ in area and if both are set in/out area.  In all non-NULL cases, the area is of _IOC_SIZE(cmd) bytes.
 */
static int efsng_ioctl(const char* pathname, int cmd, void* arg, struct fuse_file_info* file_info, unsigned int flags,
                       void* data){

    (void) pathname;
    (void) cmd;
    (void) arg;
    (void) file_info;
    (void) flags;
    (void) data;

    /* make sure we notice if this is ever used */
    assert(false);

    return 0;
}

/** 
 * Poll for IO readiness events
 *
 * Note: If ph is non-NULL, the client should notify when IO readiness events occur by calling fuse_notify_poll() with
 * the specified ph.
 *
 * Regardless of the number of times poll with a non-NULL ph is received, single notification is enough to clear all.
 * Notifying more times incurs overhead but doesn't harm correctness.
 *
 * The callee is responsible for destroying ph with fuse_pollhandle_destroy() when no longer in use.
 */
static int efsng_poll(const char* pathname, struct fuse_file_info* file_info, struct fuse_pollhandle* ph, 
                      unsigned* reventsp){

    (void) pathname;
    (void) file_info;
    (void) ph;
    (void) reventsp;

    /* make sure we notice if this is ever used */
    assert(false);

    return 0;
}


/** 
 * Write contents of buffer to an open file
 *
 * Similar to the write() method, but data is supplied in a generic buffer. Use fuse_buf_copy() to transfer data to the
 * destination.
 */
static int efsng_write_buf(const char* pathname, struct fuse_bufvec* buf, off_t offset, 
                           struct fuse_file_info* file_info){

    (void) pathname;

    auto file_record = (efsng::File*) file_info->fh;

    struct fuse_bufvec dst = FUSE_BUFVEC_INIT(fuse_buf_size(buf));

	dst.buf[0].flags = (fuse_buf_flags) (FUSE_BUF_IS_FD | FUSE_BUF_FD_SEEK);
	dst.buf[0].fd = file_record->get_fd();
	dst.buf[0].pos = offset;

	return fuse_buf_copy(&dst, buf, FUSE_BUF_SPLICE_NONBLOCK);
}

/** 
 * Store data from an open file in a buffer
 *
 * Similar to the read() method, but data is stored and returned in a generic buffer.
 *
 * No actual copying of data has to take place, the source file descriptor may simply be stored in the buffer for later
 * data transfer.
 *
 * The buffer must be allocated dynamically and stored at the location pointed to by bufp.  If the buffer contains
 * memory regions, they too must be allocated using malloc().  The allocated memory will be freed by the caller.
 */
static int efsng_read_buf(const char* pathname, struct fuse_bufvec** bufp, size_t size, off_t offset, 
                          struct fuse_file_info* file_info){

    (void) pathname;

    auto file_record = (efsng::File*) file_info->fh;

    struct fuse_bufvec* src;

    src = (struct fuse_bufvec*) malloc(sizeof(struct fuse_bufvec));

    if(src == NULL){
        return -ENOMEM;
    }

    *src = FUSE_BUFVEC_INIT(size);

    src->buf[0].flags = (fuse_buf_flags) (FUSE_BUF_IS_FD | FUSE_BUF_FD_SEEK);
    src->buf[0].fd = file_record->get_fd();
    src->buf[0].pos = offset;

    *bufp = src;

    return 0;
}

/**
 * Perform BSD file locking operation
 *
 * The op argument will be either LOCK_SH, LOCK_EX or LOCK_UN
 *
 * Nonblocking requests will be indicated by ORing LOCK_NB to the above operations
 *
 * For more information see the flock(2) manual page.
 *
 * Additionally fi->owner will be set to a value unique to this open file.  This same value will be supplied to
 * ->release() when the file is released.
 *
 * Note: if this method is not implemented, the kernel will still allow file locking to work locally.  Hence it is only
 * interesting for network filesystems and similar.
 */
static int efsng_flock(const char* pathname, struct fuse_file_info* file_info, int op){

    (void) pathname;
    (void) file_info;
    (void) op;

    /* make sure we notice if this is ever used */
    assert(false);

    return 0;
}

#ifdef HAVE_POSIX_FALLOCATE
/**
 * Allocates space for an open file
 *
 * This function ensures that required space is allocated for specified file. If this function returns success then any
 * subsequent write request to specified range is guaranteed not to fail because of lack of space on the file system
 * media.
 */
static int efsng_fallocate(const char* pathname, int, off_t, off_t, struct fuse_file_info* file_info){

    (void) pathname;
    (void) file_info;

    /* make sure we notice if this is ever used */
    assert(false);

    return 0;
}
#endif /* HAVE_POSIX_FALLOCATE */

/**********************************************************************************************************************/
/*  main point of entry                                                                                               */
/**********************************************************************************************************************/
int main (int argc, char *argv[]){

    /* 1. parse command-line arguments */
    std::shared_ptr<Arguments> user_args(new Arguments);

    for(int i=0; i<MAX_FUSE_ARGS; ++i){
        /* libfuse expects NULL args */
        user_args->fuse_argv[i] = NULL;
    }

    if(argc == 1 || !process_args(argc, argv, user_args)){
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    for(int i=0; i<user_args->fuse_argc; ++i){
        std::cerr << user_args->fuse_argv[i] << "\n";
    }

    /* 2. prepare operations */
    fuse_operations efsng_ops;
    memset(&efsng_ops, 0, sizeof(fuse_operations));

    efsng_ops.getattr = efsng_getattr;
    efsng_ops.readlink = efsng_readlink;
    /* efsng_ops.getdir = efsng_getdir; // deprecated */
    efsng_ops.mknod = efsng_mknod;
    efsng_ops.mkdir = efsng_mkdir;
    efsng_ops.unlink = efsng_unlink;
    efsng_ops.rmdir = efsng_rmdir;
    efsng_ops.symlink = efsng_symlink;
    efsng_ops.rename = efsng_rename;
    efsng_ops.link = efsng_link;
    efsng_ops.chmod = efsng_chmod;
    efsng_ops.chown = efsng_chown;
    efsng_ops.truncate = efsng_truncate;
    /* efsng_ops.utime = efsng_utime; // deprecated */
    efsng_ops.open = efsng_open;
    efsng_ops.read = efsng_read;
    efsng_ops.write = efsng_write;
    efsng_ops.statfs = efsng_statfs;
    efsng_ops.flush = efsng_flush;
    efsng_ops.release = efsng_release;
    efsng_ops.fsync = efsng_fsync;

#ifdef HAVE_SETXATTR
    efsng_ops.setxattr = efsng_setxattr;
    efsng_ops.getxattr = efsng_getxattr;
    efsng_ops.listxattr = efsng_listxattr;
    efsng_ops.removexattr = efsng_removexattr;
#endif /* HAVE_SETXATTR */

    efsng_ops.opendir = efsng_opendir;
    efsng_ops.readdir = efsng_readdir;
    efsng_ops.releasedir = efsng_releasedir;
    efsng_ops.fsyncdir = efsng_fsyncdir;
    efsng_ops.init = efsng_init;
    efsng_ops.destroy = efsng_destroy;
    efsng_ops.access = efsng_access;
    efsng_ops.create = efsng_create;
    efsng_ops.ftruncate = efsng_ftruncate;
    efsng_ops.fgetattr = efsng_fgetattr;

#ifdef HAVE_LIBULOCKMGR
    efsng_ops.lock = efsng_lock;
#endif /* HAVE_LIBULOCKMGR */

#ifdef HAVE_UTIMENSAT
    efsng_ops.utimens = efsng_utimens;
#endif /* HAVE_UTIMENSAT */

    efsng_ops.bmap = efsng_bmap;
    efsng_ops.ioctl = efsng_ioctl;
    efsng_ops.poll = efsng_poll;
    efsng_ops.write_buf = efsng_write_buf;
    efsng_ops.read_buf = efsng_read_buf;
    efsng_ops.flock = efsng_flock;

#ifdef HAVE_POSIX_FALLOCATE
    efsng_ops.fallocate = efsng_fallocate;
#endif /* HAVE_POSIX_FALLOCATE */

    /* 3. start the filesystem */
    int res = fuse_main(user_args->fuse_argc, 
                        const_cast<char **>(user_args->fuse_argv), 
                        &efsng_ops, 
                        (void*) NULL);

    std::cout << "(" << res << ") Bye!\n";

    return 0;
}
