#define _FILE_OFFSET_BITS 64
#define MEMCACHED_DEFAULT_COMMAND_SIZE 350
#define FUSE_USE_VERSION 30

#include <fuse.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <libmemcached/memcached.h>


typedef struct{
    char *host;
    char *port;
    short verbose;
    unsigned int maxhandle;
}memcachefs_opt_t;

/* default options */
memcachefs_opt_t opt = {
    .host = NULL,
    .port = "11211",
    .verbose = 0,
    .maxhandle = 10,
};

// Memcached
memcached_server_st *servers = NULL;
memcached_st *memc;
memcached_return rc;
uint32_t flags;
void *auxbuf;
// TODO: this could be a structure
long int memcached_bytes_used;

static memcached_return_t get_stat(const memcached_instance_st *memc,
                                     const char *key, size_t key_length,
                                     const char *value, size_t value_length, void *context) {
    struct statvfs *stbuf = context;

    if(opt.verbose){
        fprintf(stderr, "%s = %s\n", key, value);
    }

    if (strcmp(key, "limit_maxbytes") == 0){
        stbuf->f_blocks = atol(value);
    }

    if (strcmp(key, "curr_items") == 0){
        stbuf->f_files = atol(value);
    }

    if (strcmp(key, "bytes") == 0){
        memcached_bytes_used = atol(value);
    }

    return MEMCACHED_SUCCESS;
}

static int memcachedfs_statfs(const char *path, struct statvfs *stbuf){
    if(opt.verbose){
        fprintf(stderr, "%s(\"%s\")\n", __func__, path);
    }

    stbuf->f_bsize = 1;     /* Optimal transfer block size */
    stbuf->f_blocks = 1;    /* Total data blocks in filesystem */
    stbuf->f_bfree = 1;     /* Free blocks in filesystem */
    stbuf->f_bavail = 1;    /* Free blocks available to unprivileged user */
    stbuf->f_files = 1;     /* Total inodes in filesystem */
    stbuf->f_ffree = 1;     /* Free inodes in filesystem */
    stbuf->f_namemax = 250;  /* maximum lenght of filenames */

    rc = memcached_stat_execute(memc, NULL, get_stat, stbuf);
    stbuf->f_bfree = stbuf->f_bavail = stbuf->f_blocks - memcached_bytes_used;
    return 0;
}

static memcached_return_t process2(const memcached_st *memc, const char *k, size_t l, void *ctx) {
    fuse_fill_dir_t (*f) = ctx;
    (*f)(auxbuf, k, NULL, 0);
    return MEMCACHED_SUCCESS;
}

static int memcachedfs_getattr(const char *path, struct stat *stbuf){
    char *key;
    size_t keylen;
    void *val;
    size_t vallen;

    if(opt.verbose){
        fprintf(stderr, "%s(\"%s\")\n", __func__, path);
    }
    memset(stbuf, 0, sizeof(struct stat));

    stbuf->st_uid = fuse_get_context()->uid;
    stbuf->st_gid = fuse_get_context()->gid;
    stbuf->st_atime = time(NULL);
    stbuf->st_mtime = time(NULL);

    if(!strcmp(path, "/")){
        stbuf->st_ino = 1;
        stbuf->st_mode = S_IFDIR | 0777;
        stbuf->st_nlink = 2;
    }
    else{
        key = (char *)path + 1;
        keylen = strlen(key);

        val = memcached_get(memc, key, keylen, &vallen, &flags, &rc);
        if (rc == MEMCACHED_SUCCESS) {
            stbuf->st_mode = S_IFREG | 0666;
            stbuf->st_nlink = 1;
            stbuf->st_size = vallen;
            free(val);
        }
        else{
            return -ENOENT;
        }
    }
    return 0;
}

static int memcachedfs_opendir(const char *path, struct fuse_file_info *fi){
    if(opt.verbose){
        fprintf(stderr, "%s(\"%s\")\n", __func__, path);
    }
    return 0;
}

static int memcachedfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi){
    if(opt.verbose){
        fprintf(stderr, "%s(\"%s\")\n", __func__, path);
    }

    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    if(strcmp(path, "/") == 0){

        memcached_dump_fn cb[1] = {&process2};
        auxbuf = buf;

        rc = memcached_dump(memc, cb, &filler, 1);
    }
    return 0;
}

static int memcachedfs_releasedir(const char *path, struct fuse_file_info *fi){
    if(opt.verbose){
        fprintf(stderr, "%s(\"%s\")\n", __func__, path);
    }
    return 0;
}

static int memcachedfs_mknod(const char *path, mode_t mode, dev_t rdev){
    int ret;
    char *key;
    size_t keylen;

    if(opt.verbose){
        fprintf(stderr, "%s(\"%s\", 0%o)\n", __func__, path, mode);
    }
    if(!S_ISREG(mode)){
        return -ENOSYS;
    }

    key = (char *)path + 1;
    keylen = strlen(key);
    rc = memcached_set(memc, key, keylen, "", 0, (time_t)0, (uint32_t)0);

    if (rc == MEMCACHED_SUCCESS){
        return 0;
    }
    else{
        fprintf(stderr, "Couldn't store key: %s\n", memcached_strerror(memc, rc));
    }

    return -EIO;
}

static int memcachedfs_mkdir(const char *path, mode_t mode){
    if(opt.verbose){
        fprintf(stderr, "%s(\"%s\", 0%o)\n", __func__, path, mode);
    }
    return -ENOSYS;
}

static int memcachedfs_unlink(const char *path){
    int ret;
    char *key;
    size_t keylen;

    if(opt.verbose){
        fprintf(stderr, "%s(\"%s\")\n", __func__, path);
    }
    key = (char *)path + 1;
    keylen = strlen(key);

    rc = memcached_delete(memc, key, keylen, 0);
    if (rc != MEMCACHED_SUCCESS) {
        fprintf(stderr, "Couldn't delete key: %s\n", memcached_strerror(memc, rc));
        return -EIO;
    }

    return 0;
}

static int memcachedfs_chmod(const char* path, mode_t mode){
    if(opt.verbose){
        fprintf(stderr, "%s(\"%s\", 0%3o)\n", __func__, path, mode);
    }
    return -ENOSYS;
}

static int memcachedfs_chown(const char *path, uid_t uid, gid_t gid){
    if(opt.verbose){
        fprintf(stderr, "%s(\"%s\", %d, %d)\n", __func__, path, uid, gid);
    }
    return -ENOSYS;
}

static int memcachedfs_truncate(const char* path, off_t length){
    char *key;
    size_t keylen;

    if(opt.verbose){
        fprintf(stderr, "%s(\"%s\", %lld)\n", __func__, path, length);
    }
    if(length != 0){
        return -ENOSYS;
    }

    key = (char *)path + 1;
    keylen = strlen(key);
    rc = memcached_set(memc, key, keylen, "", 0, (time_t)0, (uint32_t)0);
    if (rc != MEMCACHED_SUCCESS) {
        fprintf(stderr, "Couldn't retrieve key: %s\n", memcached_strerror(memc, rc));
        return -EIO;
    }

    return 0;
}

static int memcachedfs_utime(const char *path, struct utimbuf *time){
    if(opt.verbose){
        fprintf(stderr, "%s(\"%s\")\n", __func__, path);
    }
    return 0;
}

static int memcachedfs_open(const char *path, struct fuse_file_info *fi){
    char *key;
    size_t keylen;
    void *value;
    size_t vallen;
    uint32_t flags;

    if(opt.verbose){
        fprintf(stderr, "%s(\"%s\")\n", __func__, path);
    }


    key = (char *)path + 1;
    keylen = strlen(key);
    value = memcached_get(memc, key, keylen, &keylen, &flags, &rc);

    if (rc != MEMCACHED_SUCCESS) {
        fprintf(stderr, "Couldn't retrieve key: %s\n", memcached_strerror(memc, rc));
        return -EIO;
    }

    return 0;
}

static int memcachedfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi){
    size_t klen;
    char *key;
    void *value;
    size_t vlen;
    uint32_t flags;

    if(opt.verbose){
        fprintf(stderr, "%s(\"%s\" %zu@%llu)\n", __func__, path, size, offset);
    }

    key = (char *)path + 1;
    klen = strlen(key);
    value = memcached_get(memc, key, klen, &vlen, &flags, &rc);
    if (rc == MEMCACHED_SUCCESS) {
        memcpy(buf, value + offset, size - offset); // TODO review size
        free(value);
    }
    else
        fprintf(stderr, "Couldn't retrieve key: %s\n", memcached_strerror(memc, rc));

     return vlen - offset;
}

static int memcachedfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi){
    size_t klen;
    char *key;
    if(opt.verbose){
        fprintf(stderr, "%s(\"%s\" %zu@%lld)\n", __func__, path, size, offset);
    }

    // TODO: for now it will overwrite everything, needs support for partial writes
    key = (char *)path + 1;
    klen = strlen(key);
    rc = memcached_set(memc, key, klen, buf, size, (time_t)0, (uint32_t)0);
    if (rc != MEMCACHED_SUCCESS){
        fprintf(stderr, "Couldn't retrieve key: %s\n", memcached_strerror(memc, rc));
        return -EIO;
    }
    return size;
}

static int memcachedfs_flush(const char *path, struct fuse_file_info *fi){
    if(opt.verbose){
        fprintf(stderr, "%s(\"%s\")\n", __func__, path);
    }
    return 0;
}

static int memcachedfs_release(const char *path, struct fuse_file_info *fi){
    if(opt.verbose){
        fprintf(stderr, "%s(\"%s\")\n", __func__, path);
    }
    return 0;
}

static int memcachedfs_fsync(const char *path, int i, struct fuse_file_info *fi){
    if(opt.verbose){
        fprintf(stderr, "%s(\"%s\", %d)\n", __func__, path, i);
    }
    return 0;
}

static int memcachedfs_link(const char *from, const char *to){
    if(opt.verbose){
        fprintf(stderr, "%s(%s, %s)\n", __func__, from, to);
    }
    return -ENOSYS;
}

static int memcachedfs_symlink(const char *from, const char *to){
    if(opt.verbose){
        fprintf(stderr, "%s(\"%s\" -> \"%s\")\n", __func__, from, to);
    }
    return -ENOSYS;
}

static int memcachedfs_readlink(const char *path, char *buf, size_t size){
    if(opt.verbose){
        fprintf(stderr, "%s(\"%s\")\n", __func__, path);
    }
    return -ENOSYS;
}

static int memcachedfs_rename(const char *from, const char *to){
    int ret;
    char *key;
    size_t keylen;
    void *value;
    size_t vallen;
    uint32_t flags;

    if(opt.verbose){
        fprintf(stderr, "%s(%s -> %s)\n", __func__, from, to);
    }

    key = (char *)from + 1;
    keylen = strlen(key);
    value = memcached_get(memc, key, keylen, &vallen, &flags, &rc);
    if (rc == MEMCACHED_SUCCESS) {
        key = (char *)to + 1;
        keylen = strlen(key);

        rc = memcached_set(memc, key, keylen, value, vallen, (time_t)0, (uint32_t)0);
        if (rc != MEMCACHED_SUCCESS){
            fprintf(stderr, "Couldn't retrieve key: %s\n", memcached_strerror(memc, rc));
            free(value);
            return -EIO;
        }
        else{
            key = (char *)from + 1;
            keylen = strlen(key);
            rc = memcached_delete(memc, key, keylen, 0);
            if (rc != MEMCACHED_SUCCESS) {
                fprintf(stderr, "Couldn't retrieve key: %s\n", memcached_strerror(memc, rc));
                free(value);
                return -EIO;;
            }
        }
    }
    else{
        fprintf(stderr, "Couldn't retrieve key: %s\n", memcached_strerror(memc, rc));
        return -ENOENT;
    }

    free(value);
    return 0;
}

static struct fuse_operations memcachedfs_oper = {
    .getattr    = memcachedfs_getattr,
    .opendir    = memcachedfs_opendir,
    .readdir    = memcachedfs_readdir,
    .releasedir = memcachedfs_releasedir,
    .mknod      = memcachedfs_mknod,
    .mkdir      = memcachedfs_mkdir,
    .unlink     = memcachedfs_unlink,
    .rmdir      = memcachedfs_unlink,
    .chmod      = memcachedfs_chmod,
    .chown      = memcachedfs_chown,
    .truncate   = memcachedfs_truncate,
    .utime      = memcachedfs_utime,
    .open       = memcachedfs_open,
    .read       = memcachedfs_read,
    .write      = memcachedfs_write,
    .flush      = memcachedfs_flush,
    .release    = memcachedfs_release,
    .fsync      = memcachedfs_fsync,
    .link       = memcachedfs_link,
    .symlink    = memcachedfs_symlink,
    .readlink   = memcachedfs_readlink,
    .rename     = memcachedfs_rename,
    .statfs     = memcachedfs_statfs,
};

void usage(){
    fprintf(stderr, "Usage: memcachedfs host[:port] mountpoint\n");
}

static int memcachedfs_opt_proc(void *data, const char *arg, int key, struct fuse_args *outargs){
    char *str;

    if(key == FUSE_OPT_KEY_OPT){
        if(!strcmp(arg, "-v")){
            opt.verbose = 1;
        }
        else if(!strncmp(arg, "maxhandle=", strlen("maxhandle="))){
            str = strchr(arg, '=') + 1;
            opt.maxhandle = atoi(str);
        }
        else{
            fuse_opt_add_arg(outargs, arg);
       }
    }else if(key == FUSE_OPT_KEY_NONOPT){
        if(!opt.host){
            opt.host = (char*)arg;
            str = strchr(arg, ':');
            if(str){
                *str = '\0';
                str++;
                opt.port = str;
            }
        }
        else{
            fuse_opt_add_arg(outargs, arg);
        }
    }
    return 0;
}

/*
 * main
 */
int main(int argc, char *argv[]){
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    fuse_opt_parse(&args, &opt, NULL, memcachedfs_opt_proc);

    if(!opt.host){
        usage();
        return EXIT_SUCCESS;
    }

    if(opt.verbose){
        fprintf(stderr, "mounting to %s:%s\n", opt.host, opt.port);
    }

    memc = memcached_create(NULL);
    servers = memcached_server_list_append(servers, opt.host, atoi(opt.port), &rc);
    rc = memcached_server_push(memc, servers);

    if (rc == MEMCACHED_SUCCESS){
        if(opt.verbose){
            fprintf(stderr, "Added server successfully\n");
        }
    }
    else{
        if(opt.verbose){
            fprintf(stderr, "Couldn't add server: %s\n", memcached_strerror(memc, rc));
        }
        return EXIT_FAILURE;
    }
    
    fuse_main(args.argc, args.argv, &memcachedfs_oper, NULL);
    fuse_opt_free_args(&args);
    return EXIT_SUCCESS;
}
