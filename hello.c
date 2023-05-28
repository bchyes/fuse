/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>

  This program can be distributed under the terms of the GNU GPL.
  See the file COPYING.
*/

/** @file
 *
 * minimal example filesystem using high-level API
 *
 * Compile with:
 *
 *     gcc -Wall hello.c `pkg-config fuse3 --cflags --libs` -o hello
 *
 * ## Source code ##
 * \include hello.c
 */


#define FUSE_USE_VERSION 31

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <assert.h>
#include <stdlib.h>

/*
 * Command line options
 *
 * We can't set default values for the char* fields here because
 * fuse_opt_parse would attempt to free() them when the user specifies
 * different values on the command line.
 */
static struct options {
	const char *filename;
	const char *contents;
	int show_help;
} options;

char msg[1024];
FILE *fp;
char msg_tmp[1024];

#define DEBUG_ON 1

#ifdef DEBUG_ON
	#define DEBUG_FILE "/fuse_result"
	#define DEBUG(x) {sprintf(msg,"%s",x); fwrite(msg, strlen(msg), 1, fp);	fflush(fp);}
	#define DEBUG_INT(x) {sprintf(msg_tmp," %d\t",(int)x);DEBUG(msg_tmp);}
	#define DEBUG_END() {sprintf(msg,"\n"); fwrite(msg, strlen(msg), 1, fp);	fflush(fp);}
#else
	#define DEBUG(x) {}
	#define DEBUG_INT(x) {}
	#define DEBUG_END() {}
#endif

#define FILE_NAME_LEN 1024
#define TOTAL_SIZE ((uint64_t)1024*1024*1024*2) // totol size of fsdemo
#define BANK_SIZE (1024*1024*4)
#define BANK_NUM (TOTAL_SIZE/BANK_SIZE)
#define CHUNK_SIZE (1024*16)
#define CHUNK_NUM (TOTAL_SIZE/CHUNK_SIZE)

struct context{
	int chunk_index;
	size_t size;
	struct context * next;
};

struct inode{
	char filename[FILE_NAME_LEN];
	size_t size;
	time_t timeLastModified;
	char isDirectories;	
	struct context* context;
	struct inode *son;
	struct inode *bro;
};
struct inode_list{
	struct inode_list *next;
	char isDirectories;
	char filename[FILE_NAME_LEN];
};
struct inode *root;
void *bank[BANK_NUM];
char bitmap[CHUNK_NUM];
char *local_buf;

struct attr{
	int size;
	time_t timeLastModified;
	char isDirectories;
};

#define OPTION(t, p)                           \
    { t, offsetof(struct options, p), 1 }
static const struct fuse_opt option_spec[] = {
	OPTION("--name=%s", filename),
	OPTION("--contents=%s", contents),
	OPTION("-h", show_help),
	OPTION("--help", show_help),
	FUSE_OPT_END
};

/* brief: deal path to dir/file mode
 * transfaring	(/root/lhd/node)  to (/root/lhd/ node)
 * transfaring 	(/root/lhd/node/) to (/root/lhd/ node)
 * do not do anything with root(/) dir 
 * 			*/
void deal(const char *path, char *dirname, char *filename){
	int i = 0, j = 0, k = 0, m = 0;
	int len = strlen(path);
	for (;i < len;i++){
		dirname[i] = path[i];
		if (path[i] == '/' && path[i + 1] != 0) j = i;
	}
	m = j++;
	for (;j < len;){
		filename[k] = path[j];
		k++; j++;
	}
	dirname[m + 1] = 0;
	if (filename[k - 1] == '/') k--;
	filename[k] = 0;
}

struct inode *get_father_inode(char *dirname){
	struct inode *head = root;
	char s[FILE_NAME_LEN];
	int i = 1, j, is_find = 0;
	if (strlen(dirname) == 1)
		return head;
	int len = strlen(dirname);
	if (dirname[len - 1] != '/'){
		dirname[len] = '/';
		dirname[len + 1] = 0;
	}
	while (1){
		head = head -> son;
		if (head == NULL) break;
		j = 0;
		while (1){
			s[j++] = dirname[i++];
			if (dirname[i] == '/'){
				s[j] = 0;
				while (1){
					if (strcmp(head -> filename,s) == 0){
						is_find = 1;
						break;
					}
					head = head -> bro;
					if (head == NULL) return NULL;
				}
			}
			if (is_find == 1){
				is_find = 0;
				if (!dirname[i + 1]){
					is_find = 1;
				}
				break;
			}
		}
		if (!dirname[++i]) break;
	}
	if (is_find == 1){
		return head;
	}
	return NULL;
}


int getFreeChunk(){
	int i = 0;
	for (;i < CHUNK_NUM;i++){
		if (bitmap[i] == 1) continue;
		break;
	}
	if (i == CHUNK_NUM){
		DEBUG("No space for free chunk");
		DEBUG_END();
	}
	bitmap[i] = 1;
	DEBUG("get free chunk = ");
	DEBUG_INT(i);
	DEBUG_END();
	return i;
}

static void *hello_init(struct fuse_conn_info *conn,
			struct fuse_config *cfg)
{	
	//(void) conn;
	//cfg->kernel_cache = 1;
	DEBUG("begin init");
	DEBUG_END();
	int init_bank_i;
	for (init_bank_i = 0;init_bank_i < BANK_NUM;init_bank_i++){
		bank[init_bank_i] = malloc(BANK_SIZE);
	}
	root = (struct inode*) malloc(sizeof(struct inode));
	root -> bro = NULL;
	root -> son = NULL;
	root -> isDirectories = 1;
	root -> size = 0;
	root -> timeLastModified = time(NULL);
	memset(root -> filename, 0,FILE_NAME_LEN);
	root -> filename[0] = '/';
	memset(bitmap, 0,sizeof(bitmap));
	return NULL;
}

int GetAttr(const char *path,struct attr *attr){
	char filename[FILE_NAME_LEN];
	int len = strlen(path);
	strcpy(filename, path);
	filename[len] = '/';
	filename[len + 1] = 0;
	struct inode* head = get_father_inode(filename);
	if (head == NULL) return -1;
	attr -> isDirectories = head -> isDirectories;
	attr -> size = head -> size;
	attr -> timeLastModified = head -> timeLastModified;
	return 0;
}

static int hello_getattr(const char *path, struct stat *stbuf,
			 struct fuse_file_info *fi)
{
	/* (void) fi;
	int res = 0;

	memset(stbuf, 0, sizeof(struct stat));
	if (strcmp(path, "/") == 0) {
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
	} else if (strcmp(path+1, options.filename) == 0) {
		stbuf->st_mode = S_IFREG | 0444;
		stbuf->st_nlink = 1;
		stbuf->st_size = strlen(options.contents);
	} else
		res = -ENOENT;

	return res; */
	DEBUG("begin getattr");
	DEBUG_END();
	struct attr attr;
	int ret = 0;
	if (strlen(path) == 1){
		attr.size = 0;
		attr.isDirectories = root -> isDirectories;
		attr.timeLastModified = root -> timeLastModified;
	} else {
		ret = GetAttr(path, &attr);
	}
	if (ret < 0){
		return -2;
	}
	if (attr.isDirectories == 1){
		stbuf -> st_mode = S_IFDIR | 0666;
		stbuf -> st_size = 0;
	} else {
		stbuf -> st_mode = S_IFREG | 0777;
		stbuf -> st_size = attr.size;
	}
	stbuf->st_nlink = 1;            /* Count of links, set default one link. */
    stbuf->st_uid = 0;              /* User ID, set default 0. */
    stbuf->st_gid = 0;              /* Group ID, set default 0. */
    stbuf->st_rdev = 0;             /* Device ID for special file, set default 0. */
    stbuf->st_atime = 0;            /* Time of last access, set default 0. */
    stbuf->st_mtime = attr.timeLastModified; /* Time of last modification, set default 0. */
    stbuf->st_ctime = 0;            /* Time of last creation or status change, set default 0. */
	return 0;
}

int ReadDir(const char *path,struct inode_list *Li){
	char filename[FILE_NAME_LEN];
	struct inode_list *list = NULL;
	int len = strlen(path);
	Li -> isDirectories = -1;
	Li -> next = NULL;
	struct inode* head;
	if (len != 1){
		strcpy(filename, path);
		head = get_father_inode(filename);
	} else head = root;
	if (head == NULL) return -1;
	head = head -> son;
	if (head != NULL){
		Li -> isDirectories = head -> isDirectories;
		strcpy(Li -> filename, head -> filename);
		Li -> next = NULL;
		head = head -> bro;
	}
	list = Li;
	while (head != NULL){
		list -> next = malloc(sizeof(struct inode_list));
		list = list -> next;
		list -> isDirectories = head -> isDirectories;
		strcpy(list -> filename, head -> filename);
		list -> next = NULL;
		head = head -> bro;
	}
	return 0;
}

static int hello_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			 off_t offset, struct fuse_file_info *fi,
			 enum fuse_readdir_flags flags)
{
	/* (void) offset;
	(void) fi;
	(void) flags;

	DEBUG(path);

	if (strcmp(path, "/") != 0)
		return -ENOENT;

	filler(buf, ".", NULL, 0, 0);
	filler(buf, "..", NULL, 0, 0);
	filler(buf, options.filename, NULL, 0, 0);

	return 0; */

	DEBUG("begin readdir");
	DEBUG_END();
	struct inode_list list,*tmp;
	struct stat st;
	ReadDir(path, &list);
	tmp = &list;
	if (tmp -> isDirectories == -1) return 0;
	for (;tmp != NULL;tmp = tmp -> next){
		memset(&st, 0,sizeof(st));
		st.st_mode = (tmp -> isDirectories == 1) ? S_IFDIR : S_IFMT;
		if (filler(buf, tmp -> filename, &st, 0, 0)) break;
	}
	return 0;

}

static int hello_open(const char *path, struct fuse_file_info *fi){
	/*if (strcmp(path+1, options.filename) != 0)
		return -ENOENT;

	if ((fi->flags & O_ACCMODE) != O_RDONLY)
		return -EACCES;*/

	return 0;
}

void Read_from_bank(int chunk_index, char *buf, size_t size, off_t chunk_offset){
	void *tbank = (bank[chunk_index/(CHUNK_NUM / BANK_NUM)]);
	int times = chunk_index % (CHUNK_NUM / BANK_NUM);
	off_t offset = chunk_offset + times * CHUNK_SIZE;
	size_t Tsize = chunk_offset + size + times * CHUNK_SIZE;
	memcpy(local_buf, tbank, Tsize);
	memcpy(buf, local_buf + offset, size);
}

static int hello_read(const char *path, char *buf, size_t size, off_t offset,
		      struct fuse_file_info *fi)
{
	/* size_t len;
	(void) fi;
	if(strcmp(path+1, options.filename) != 0)
		return -ENOENT;

	len = strlen(options.contents);
	if (offset < len) {
		if (offset + size > len)
			size = len - offset;
		memcpy(buf, options.contents + offset, size);
	} else
		size = 0;

	return size; */

	DEBUG("begin read");
	DEBUG_END();
	char *bbuf = malloc(size);
	char filename[FILE_NAME_LEN];
	int len = strlen(path);
	strcpy(filename, path);
	filename[len] = '/';
	filename[len + 1] = 0;
	struct inode *head = get_father_inode(filename);
	if (head == NULL || head -> isDirectories == 1) return -1;
	struct context *cnt = head -> context;
	if (cnt == NULL) return 0;
	off_t read_offset = offset;
	int i = read_offset / CHUNK_SIZE;
	DEBUG("i:");
	DEBUG_INT(i);
	DEBUG_END();
	DEBUG_INT(size);
	DEBUG_INT(cnt -> size);
	while (i > 0){
		if (cnt == NULL || cnt -> size != CHUNK_SIZE){
			DEBUG("file system read error1\n"); 
			return 0;
		}
		cnt = cnt -> next;
		i--;
	}
	read_offset = read_offset % CHUNK_SIZE;
	DEBUG_INT(read_offset);
	size_t read_size = 0, un_read_size = size;
	while (un_read_size > 0){
		if (cnt == NULL){
			break; //a read will read a page size
		}
		if (un_read_size >= cnt -> size - read_offset){
			Read_from_bank(cnt -> chunk_index, bbuf + read_size, cnt -> size - read_offset, read_offset);
			read_size += cnt -> size - read_offset;
			un_read_size -= cnt -> size - read_offset;
			read_offset = 0;
		} else {
			Read_from_bank(cnt -> chunk_index, bbuf + read_size, un_read_size, read_offset);
			read_size += un_read_size;
			un_read_size = 0;
		}
		cnt = cnt -> next;
	}
	bbuf[read_size] = 0;
	memcpy(buf,bbuf,read_size);
	free(bbuf);
	return read_size;
}

static int hello_access(const char *path, int mask){
	return 0;
}

int CreateDirectory(const char *path){
	if (strlen(path) == 0) return 0;
	char filename[FILE_NAME_LEN], dirname[FILE_NAME_LEN];
	deal(path, dirname, filename);
	struct inode * now = malloc(sizeof(struct inode));
	now -> isDirectories = 1;
	now -> son = NULL;
	now -> size = 0;
	now -> timeLastModified = time(NULL);
	memset(now -> filename, 0, FILE_NAME_LEN);
	strcpy(now -> filename, filename);
	struct inode *father = get_father_inode(dirname);
	if (father == NULL) return -1;
	if (father -> isDirectories == 0) return -1;
	struct inode *tmp = father -> son;
	father -> son = now;
	now -> bro = tmp;
	return 1;
}

static int hello_mkdir(const char *path, mode_t mode){
	DEBUG("begin mkdir");
	DEBUG_END();
	int res = CreateDirectory(path);
	DEBUG_INT(res);
	DEBUG_END();
	if (res < 0)
		return -1;
	else
		return 0;
}

int CreateFile(const char *path){
	if (strlen(path) == 1){
		return 0;
	}
	char filename[FILE_NAME_LEN], dirname[FILE_NAME_LEN];
	deal(path,dirname,filename);
	struct inode *father = get_father_inode(dirname);
	if (father == NULL){
		DEBUG("Error path to MKnod\n");
		return -1;
	}
	{
		char filename[FILE_NAME_LEN];
		int len = strlen(path);
		strcpy(filename, path);
		filename[len] = '/';
		filename[len + 1] = 0;
		struct inode* head = get_father_inode(filename);
		if (head != NULL){
			DEBUG("MKnod same file fail\n");
			return -1;
		}
	}
	struct inode* now = malloc(sizeof(struct inode));
	now -> isDirectories = 0;
	now -> son = NULL;
	now -> bro = NULL;
	now -> size = 0;
	now -> timeLastModified = time(NULL);
	now -> context = NULL;
	memset(now -> filename, 0, FILE_NAME_LEN);
	strcpy(now -> filename, filename);
	struct inode* tmp = father -> son;
	DEBUG("CreateFile");
	DEBUG_END();
	father -> son = now;
	now -> bro = tmp;
	return 1;
}

static int hello_mknod(const char *path, mode_t mode, dev_t rdev){
	DEBUG("begin mknod\n");
	DEBUG(path);
	DEBUG_END();
	int res = CreateFile(path);
	DEBUG_INT(res);
	DEBUG_END();
	if (res < 0){
		return -1;
	} else {
		return 0;
	}
}

void FreeInode(struct inode *head){
	if (head == NULL) return;
	struct context *context = head -> context, *tmp;
	while (context != NULL){
		tmp = context;
		bitmap[tmp -> chunk_index] = 0;
		context = context -> next;
		free(tmp);
	}
	free(head);
}

void DeleteAll(struct inode *head){
	if (head == NULL) return;
	if (head -> bro != NULL){
		DeleteAll(head -> bro);
	}
	if (head -> isDirectories == 1 && head -> son != NULL){
		DeleteAll(head -> son);
	}
	FreeInode(head);
}

int DelFromInode(struct inode *head,char *filename){
	struct inode *tmp, *ttmp;
	tmp = head -> son;
	if (tmp == NULL) return 0;
	if (strcmp(filename, tmp -> filename) == 0){
		head -> son = tmp -> bro;
		tmp -> bro = NULL;
		if (tmp -> isDirectories == 1)
			DeleteAll(tmp -> son);
		FreeInode(tmp);
		return 0;
	}
	while (tmp -> bro != NULL){
		ttmp = tmp;
		tmp = tmp -> bro;
		if (strcmp(filename, tmp -> filename) == 0){
			ttmp -> bro = tmp -> bro;
			tmp -> bro = NULL;
			if (tmp -> isDirectories == 1)
				DeleteAll(tmp -> son);
			FreeInode(tmp);
			return 0;
		}
	}
	return -1;
}

int Delete(const char *path){
	if (strlen(path) == 1) return 0;
	char filename[FILE_NAME_LEN], dirname[FILE_NAME_LEN];
	deal(path, dirname, filename);
	struct inode *father = get_father_inode(dirname);
	if (father == NULL) return -1;
	return DelFromInode(father, filename);
}

static int hello_rmdir(const char *path){
	DEBUG("begin rmdir");
	DEBUG_END();
	int res = Delete(path);
	if (res < 0){
		return -2;
	} else {
		return 0;
	}
}

static int hello_unlink(const char *path){
	DEBUG("begin unlink");
	DEBUG_END();
	int res = Delete(path);
	if (res < 0){
		return -2;
	} else {
		return 0;
	}
}

void Write_to_bank(int chunk_index, const char *buf, size_t size, off_t chunk_offset){
	DEBUG("begin write_to_bank:");
	DEBUG(buf);
	DEBUG_END();
	DEBUG_INT(chunk_index);
	DEBUG_END();
	DEBUG_INT(size);
	DEBUG_END();
	DEBUG_INT(chunk_offset);
	DEBUG_END();
	void *tbank = bank[chunk_index / (CHUNK_NUM / BANK_NUM)];
	int times = chunk_index % (CHUNK_NUM / BANK_NUM);
	off_t offset = chunk_offset + times * CHUNK_SIZE;
	size_t Tsize = chunk_offset + size + times * CHUNK_SIZE;
	DEBUG("end write_to_bank:1");
	memcpy(local_buf, tbank, offset);
	DEBUG("end write_to_bank:2");
	memcpy(local_buf + offset, buf, size);
	DEBUG("end write_to_bank:3");
	DEBUG(local_buf)
	memcpy(tbank, local_buf, Tsize);
	DEBUG("end write_to_bank:");
}

static int hello_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi){
	DEBUG("begin write:");
	DEBUG(buf);
	DEBUG_END();
	DEBUG_INT(size);
	DEBUG_END();
	DEBUG_INT(offset);
	DEBUG_END();
	char filename[FILE_NAME_LEN];
	int len = strlen(path);
	strcpy(filename, path);
	filename[len] = '/';
	filename[len + 1] = 0;
	struct inode *head = get_father_inode(filename);
	if (head == NULL || head -> isDirectories == 1) return -1;
	struct context *cnt = head -> context;
	if (cnt == NULL){
		head -> context = malloc(sizeof(struct context));
		cnt = head -> context;
		cnt -> size = 0;
		cnt -> chunk_index = getFreeChunk();
		cnt -> next = NULL;
	}
	off_t write_offset = offset;
	int i = write_offset / CHUNK_SIZE;
	write_offset = write_offset % CHUNK_SIZE;
	while (i > 0){
		if (cnt == NULL) return 0;
		if (cnt -> size != CHUNK_SIZE) return 0;
		i--;
		if (i == 0 && write_offset == 0 && cnt -> next == NULL){
			cnt -> next = malloc(sizeof(struct context));
			cnt -> next -> size = 0;
			cnt -> next -> chunk_index = getFreeChunk();
			cnt -> next -> next = NULL;
		}
		cnt = cnt -> next;
	}
	size_t write_size = 0, un_write_size = size;
	while (un_write_size > 0){
		if (un_write_size >= CHUNK_SIZE - write_offset){
			Write_to_bank(cnt -> chunk_index, buf + write_size, (CHUNK_SIZE - write_offset), write_offset);
			write_size += CHUNK_SIZE - write_offset;
			un_write_size -= (CHUNK_SIZE - write_offset);
			head -> size = head -> size - cnt -> size + CHUNK_SIZE;
			write_offset = 0;
			cnt -> size = CHUNK_SIZE;
			if (cnt -> next == NULL && un_write_size > 0){
				cnt -> next = malloc(sizeof(struct context));
				cnt -> next -> size = 0;
				cnt -> next -> chunk_index = getFreeChunk();
				cnt -> next -> next = NULL;
			}
		} else {
			Write_to_bank(cnt -> chunk_index, buf + write_size, un_write_size, write_offset);
			write_size += un_write_size;
			if (cnt -> size < write_offset){//modified
				return 0;
			}
			if (cnt -> size < un_write_size + write_offset){
				head -> size = head -> size - cnt -> size;
				cnt -> size = un_write_size + write_offset;
				head -> size += cnt -> size;
			}
			un_write_size = 0;
		}
	}
	DEBUG("write finished");
	return size;
}

static int hello_statfs(const char *path, struct statvfs *stbuf){
	stbuf->f_bsize = BANK_SIZE;
	return 0;
}

static int hello_chmod(const char *path, mode_t mode,
		     struct fuse_file_info *fi){
	return 0;
}

static int hello_chown(const char *path, uid_t uid, gid_t gid,
		     struct fuse_file_info *fi){
	return 0;
}

static int hello_truncate(const char *path, off_t size,
			struct fuse_file_info *fi){
	return 0;
}

static int hello_rename(const char *from, const char *to, unsigned int flag){
	char filename[FILE_NAME_LEN], dirname[FILE_NAME_LEN], toname[FILE_NAME_LEN];
	int len = strlen(from);
	strcpy(filename, from);
	filename[len] = '/';
	filename[len + 1] = 0;
	struct inode *head = get_father_inode(filename);
	if (head == NULL){
		return -17;
	}
	deal(to, dirname, filename);
	strcpy(head -> filename, toname);
	return 0;
}

static int hello_create(const char *path, mode_t mode, struct fuse_file_info *fi){
	DEBUG("begin create");
	DEBUG(path);
	DEBUG_END();
	int res = CreateFile(path);
	DEBUG_INT(res);
	DEBUG_END();
	if (res < 0){
		return -1;
	} else {
		return 0;
	}
}

static int hello_setxattr(const char *path, const char *name, const char *value, size_t size, int flag){
	DEBUG("begin setxattr");
	DEBUG(path);
	DEBUG_END();
	DEBUG(name);
	DEBUG_END();
	return 0;
}

static int hello_utimens(const char *path, const struct timespec tv[2], struct fuse_file_info *fi){
	DEBUG("begin utimens");
	DEBUG(path);
	DEBUG_END();
	return 0;
}


static struct fuse_operations hello_oper = {
	.init           = hello_init,
	.getattr	= hello_getattr,
	.readdir	= hello_readdir,
	.open		= hello_open,
	.read		= hello_read,
	.access 	= hello_access,
	.mknod 		= hello_mknod,
	.unlink 	= hello_unlink,
	.mkdir 		= hello_mkdir,
	.rmdir 		= hello_rmdir,
	.statfs		= hello_statfs,
	.write 		= hello_write,
	.chmod 		= hello_chmod,
	.chown 		= hello_chown,
	.truncate 	= hello_truncate,
	.rename 	= hello_rename,
	.create 	= hello_create,
	.setxattr 	= hello_setxattr,
	.utimens 	= hello_utimens,
};

static void show_help(const char *progname)
{
	printf("usage: %s [options] <mountpoint>\n\n", progname);
	printf("File-system specific options:\n"
	       "    --name=<s>          Name of the \"hello\" file\n"
	       "                        (default: \"hello\")\n"
	       "    --contents=<s>      Contents \"hello\" file\n"
	       "                        (default \"Hello, World!\\n\")\n"
	       "\n");
}

int main(int argc, char *argv[])
{
	int ret;
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

	/* Set defaults -- we have to use strdup so that
	   fuse_opt_parse can free the defaults if other
	   values are specified */
	options.filename = strdup("hello");
	options.contents = strdup("Hello World!\n");

	/* Parse options */
	if (fuse_opt_parse(&args, &options, option_spec, NULL) == -1)
		return 1;

	/* When --help is specified, first print our own file-system
	   specific help text, then signal fuse_main to show
	   additional help (by adding `--help` to the options again)
	   without usage: line (by setting argv[0] to the empty
	   string) */
	if (options.show_help) {
		show_help(argv[0]);
		assert(fuse_opt_add_arg(&args, "--help") == 0);
		args.argv[0][0] = '\0';
	}

	#ifdef DEBUG_ON
		fp = fopen(DEBUG_FILE, "w");
		fclose(fp);
		fp = fopen(DEBUG_FILE, "ab+");
	#endif
	local_buf = malloc(BANK_SIZE+1);

	ret = fuse_main(args.argc, args.argv, &hello_oper, NULL);
	fuse_opt_free_args(&args);
	return ret;
}
