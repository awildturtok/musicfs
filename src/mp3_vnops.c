/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
#define FUSE_USE_VERSION 26
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <err.h>

#include <sys/types.h>
#include <dirent.h>
#include <fuse.h>
#include <sys/param.h>
#include <sys/uio.h>
#include <unistd.h>

#include <tag_c.h>
#include <musicfs.h>
#include <debug.h>

char musicpath[MAXPATHLEN]; // = "/home/lulf/dev/musicfs/music";
char *logpath = "/home/lulf/dev/musicfs/musicfs.log";

static int mfs_getattr (const char *path, struct stat *stbuf)
{
	struct file_data fd;
	fd.fd = -1;
	fd.found = 0;

	int status = 0;
	memset (stbuf, 0, sizeof (struct stat));

	if (strcmp (path, "/") == 0) {
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
		return 0;
	}

	enum mfs_filetype type = mfs_get_filetype(path);
	switch (type) {
	case MFS_DIRECTORY:
		stbuf->st_mode = S_IFDIR | 0444;
		stbuf->st_nlink = 1;
		stbuf->st_size = 12;
		return 0;

	case MFS_FILE:
		status = mfs_file_data_for_path(path, &fd);
		if (status != 0)
			return (status);

		if (!fd.found)
			return (-ENOENT);
		if (fd.fd < 0)
			return (-EIO);

		if (fstat(fd.fd, stbuf) == 0)
			return (0);
		else
			return (-ENOENT);

	case MFS_NOTFOUND:
	default:
		return -ENOENT;
	}
}


static int mfs_readdir (const char *path, void *buf, fuse_fill_dir_t filler,
						off_t offset, struct fuse_file_info *fi)
{
	struct filler_data fd;
	struct lookuphandle *lh;

	filler (buf, ".", NULL, 0);
	filler (buf, "..", NULL, 0);
	fd.buf = buf;
	fd.filler = filler;

	if (!strcmp(path, "/")) {
		filler(buf, "Artists", NULL, 0);
		filler(buf, "Genres", NULL, 0);
		filler(buf, "Tracks", NULL, 0);
		filler(buf, "Albums", NULL, 0);
		return (0);
	}

	/*
	 * 1. Parse the path.
	 * 2. Find the mp3s that matches the tags given from the path.
	 * 3. Return the list of those mp3s.
	 */
	if (strncmp(path, "/Artists", 8) == 0) {
		mfs_lookup_artist(path, &fd);
		return (0);
	} else if (strncmp(path, "/Genres", 7) == 0) {
		mfs_lookup_genre(path, &fd);
		return (0);
	} else if (strcmp(path, "/Tracks") == 0) {
		lh = mfs_lookup_start(0, &fd, mfs_lookup_list,
		    "SELECT DISTINCT artistname||' - '||title||'.'||extension "
		    "FROM song");
		mfs_lookup_finish(lh);
		return (0);
	} else if (strncmp(path, "/Albums", 7) == 0) {
		mfs_lookup_album(path, &fd);
		return (0);
	}

	return (-ENOENT);
}

static int mfs_open (const char *path, struct fuse_file_info *fi)
{
	struct file_data fd;
	fd.fd = -1;
	fd.found = 0;

	int status = mfs_file_data_for_path(path, &fd);
	if (status != 0)
		return (status);

	if (!fd.found)
		return (-ENOENT);
	if (fd.fd < 0)
		return (-EIO);
	close(fd.fd);

	return (0);

	/*
	 * 1. Have a lookup cache for names?.
	 *    Parse Genre, Album, Artist etc.
	 * 2. Find MP3s that match. XXX what do we do with duplicates? just
	 *    return the first match?
	 * 3. Put the mnode of the mp3 in our cache.
	 * 4. Signal in mnode that the mp3 is being read?
	 */
}

static int mfs_read (const char *path, char *buf, size_t size, off_t offset,
					 struct fuse_file_info *fi)
{
	struct file_data fd;
	fd.fd = -1;
	fd.found = 0;
	size_t bytes;

	int status = mfs_file_data_for_path(path, &fd);
	if (status != 0)
		return (status);

	if (!fd.found)
		return (-ENOENT);
	if (fd.fd < 0)
		return (-EIO);
	lseek(fd.fd, offset, SEEK_CUR);
	bytes = read(fd.fd, buf, size);
	close(fd.fd);
	return (bytes);

	/*
	 * 1. Find the mnode given the path. If not in cache, read through mp3
	 *    list to find it. 
	 * 2. Read the offset of the mp3 and return the data.
	 */
}

static struct fuse_operations mfs_ops = {
	.getattr	= mfs_getattr,
	.readdir	= mfs_readdir,
	.open		= mfs_open,
	.read		= mfs_read,
};

static int musicfs_opt_proc (void *data, const char *arg, int key,
						   struct fuse_args *outargs)
{
	static int musicpath_set = 0;

	if (key == FUSE_OPT_KEY_NONOPT && !musicpath_set) {
		/* The source directory isn't already set, let's do it */
		strcpy(musicpath, arg);
		musicpath_set = 1;
		return (0);
	}
	return (1);
}

int
mfs_run(int argc, char **argv)
{
	int ret;
	/*
	 * XXX: Build index of mp3's.
	 */

	/* Update tables. */
	if (argc < 2) {
		fprintf(stderr, "Usage: %s <musicfolder> <mountpoint>\n", argv[0]); 
		return (-1);
	}

	struct fuse_args args = FUSE_ARGS_INIT (argc, argv);

	if (fuse_opt_parse(&args, NULL, NULL, musicfs_opt_proc) != 0)
		exit (1);
		
	DEBUG("musicpath: %s\n", musicpath);
	mfs_initscan(musicpath);

	ret = 0;
	ret = fuse_main(args.argc, args.argv, &mfs_ops, NULL);
	return (ret);
}
