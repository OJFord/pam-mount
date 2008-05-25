/*
 *	Copyright © Elvis Pfützenreuter, 2000
 *	Copyright © Jan Engelhardt, 2006 - 2008
 *	Copyright © Bastian Kleineidam, 2005
 *
 *	This file is part of pam_mount; you can redistribute it and/or
 *	modify it under the terms of the GNU Lesser General Public License
 *	as published by the Free Software Foundation; either version 2.1
 *	of the License, or (at your option) any later version.
 */
#include <config.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libHX.h>
#include <pwd.h>
#include "compiler.h"
#include "crypto.h"
#include "misc.h"
#include "mount.h"
#include "pam_mount.h"
#include "private.h"
#include "readconfig.h"
#include "spawn.h"
#include "xstdlib.h"
#if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__APPLE__)
#	include <fstab.h>
#elif defined(__linux__)
#	include <mntent.h>
#	include <sys/ioctl.h>
#	include <fcntl.h>
#	include <linux/loop.h>
#	include <linux/major.h>
#endif
#ifndef S_IXUGO
#	define S_IXUGO (S_IXUSR | S_IXGRP | S_IXOTH)
#endif

/* Functions */
static int already_mounted(const struct config * const, const unsigned int, struct HXbtree *);
static int check_filesystem(const struct config *, const unsigned int, struct HXbtree *, const unsigned char *, size_t);
static int do_losetup(const struct config *, const unsigned int, struct HXbtree *, const unsigned char *, size_t);
static int do_unlosetup(const struct config *, struct HXbtree *);
static int fstype_nodev(const char *);
static void log_pm_input(const struct config * const, const unsigned int);
static inline bool mkmountpoint(struct vol *, const char *);
static int pipewrite(int, const void *, size_t);
static void run_lsof(const struct config * const, struct HXbtree *);
static void vol_to_dev(char *, size_t, const struct vol *);

#ifdef HAVE_STRUCT_LOOP_INFO64_LO_FILE_NAME
static inline const char *loop_bk(const char *, struct loop_info64 *);
#endif
#if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__APPLE__)
static int split_bsd_mount(char *, const char **, const char **, const char **);
#endif

//-----------------------------------------------------------------------------
/*
 * log_output
 * @fd:		file descriptor to read from
 * @cmsg:	conditional message
 *
 * Reads all data from @fd and logs it using w4rn(). @fd is usually connected
 * to a pipe to another process's stdout or stderr. Only if @fd actually has
 * output for us, @cmsg will be printed.
 *
 * @fd will be closed.
 */
static void log_output(int fd, const char *cmsg)
{
	char buf[BUFSIZ + 1];
	FILE *fp;

	if ((fp = fdopen(fd, "r")) == NULL) {
		w4rn("error opening file: %s\n", strerror(errno));
		return;
	}

	setvbuf(fp, NULL, _IOLBF, 0);
	if (fgets(buf, sizeof(buf), fp) != NULL) {
		if (cmsg != NULL)
			w4rn("%s", cmsg);

		do {
			w4rn("%s", buf);
		} while (fgets(buf, sizeof(buf), fp) != NULL);
	}
	fclose(fp);
	return;
}

/*
 * run_lsof
 * @config:	current configuration
 * @vinfo:
 *
 * Runs `lsof` on a directory/mountpoint and logs its output, for debugging
 * purposes.
 */
static void run_lsof(const struct config *const config,
    struct HXbtree *vinfo)
{
	int _argc = 0, cstdout = -1;
	const char *_argv[MAX_PAR + 1];
	struct stat sb;
	unsigned int i;
	pid_t pid;

	if (stat(HXbtree_get(vinfo, "MNTPT"), &sb) < 0 && errno == EEXIST)
		return;

	if (config->command[CMD_LSOF][0] == NULL)
		l0g("lsof not defined in pam_mount.conf.xml\n");
	/* FIXME: NEW */
	for (i = 0; config->command[CMD_LSOF][i] != NULL; ++i)
		add_to_argv(_argv, &_argc, config->command[CMD_LSOF][i], vinfo);
	log_argv(_argv);

	if (!spawn_start(_argv, &pid, NULL, &cstdout, NULL, NULL, NULL))
		return;

	log_output(cstdout, "lsof output:\n");
	w4rn("waiting for lsof\n");
	if (waitpid(pid, NULL, 0) < 0)
		l0g("error waiting for child: %s\n", strerror(errno));
	spawn_restore_sigchld();
	return;
}


/* already_mounted
 * @config:	current config
 * @vol:	volume index into @config->volume[]
 * @vinfo:
 *
 * Checks if @config->volume[@vol] is already mounted, and returns 1 if this
 * the case, 0 if not and -1 on error.
 */
static int already_mounted(const struct config *const config,
    const unsigned int vol, struct HXbtree *vinfo)
#if defined(__linux__)
{
	char dev[PATH_MAX+1] = {}, real_mpt[PATH_MAX+1];
	struct mntent *mtab_record;
	bool mounted = false;
	FILE *mtab;
	struct vol *vpt;

	assert(config_valid(config));
	vpt = &config->volume[vol];
	vol_to_dev(dev, sizeof(dev), vpt);

	if ((mtab = setmntent("/etc/mtab", "r")) == NULL) {
		l0g("could not open /etc/mtab\n");
		return -1;
	}
	if (realpath(vpt->mountpoint, real_mpt) == NULL) {
		w4rn("can't get realpath of volume %s: %s\n",
		     vpt->mountpoint, strerror(errno));
		strncpy(real_mpt, vpt->mountpoint, sizeof_z(real_mpt));
		real_mpt[sizeof_z(real_mpt)] = '\0';
	} else {
		real_mpt[sizeof_z(real_mpt)] = '\0';
		l0g("realpath of volume \"%s\" is \"%s\"\n",
		    vpt->mountpoint, real_mpt);
	}

	w4rn("checking to see if %s is already mounted at %s\n",
	     dev, vpt->mountpoint);

	while ((mtab_record = getmntent(mtab)) != NULL) {
		const char *fsname = mtab_record->mnt_fsname;
		const char *fstype = mtab_record->mnt_type;
		const char *fspt   = mtab_record->mnt_dir;
		int (*xcmp)(const char *, const char *);
		struct loop_info64 loopdev;
		struct stat statbuf;

#ifdef HAVE_STRUCT_LOOP_INFO64_LO_FILE_NAME
		if (stat(fsname, &statbuf) == 0 && S_ISBLK(statbuf.st_mode) &&
		    major(statbuf.st_rdev) == LOOP_MAJOR)
			/*
			 * If /etc/mtab is a link to /proc/mounts then the loop
			 * device instead of the real device will be listed --
			 * resolve it.
			 */
			fsname = loop_bk(fsname, &loopdev);
#endif

		xcmp = (strcmp(fstype, "smbfs") == 0 ||
		        strcmp(fstype, "cifs") == 0 ||
		        strcmp(fstype, "ncpfs") == 0) ? strcasecmp : strcmp;

		if (xcmp(fsname, dev) == 0 &&
		    (strcmp(fspt, vpt->mountpoint) == 0 ||
		    strcmp(fspt, real_mpt) == 0)) {
			mounted = true;
			break;
		}
	}

	endmntent(mtab);
	return mounted;
}
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__APPLE__)
{
	char *_argv[MAX_PAR + 1], dev[PATH_MAX+1] = {}, mte[BUFSIZ + 1];
	int i, _argc = 0, cstdout = -1, mounted = 0;
	struct vol *vpt;
	pid_t pid;
	FILE *fp;

	assert(config_valid(config));
	vpt = &config->volume[vol];
	vol_to_dev(dev, sizeof(dev), vpt);

	/*
	 * WONTFIX: I am not overly fond of using mount, but BSD has no
	 * /etc/mtab?
	 */
	if (config->command[CMD_MNTCHECK][0] == NULL) {
		l0g("mntcheck not defined in pam_mount.conf.xml\n");
		return -1;
	}

	for (i = 0; config->command[CMD_MNTCHECK][i] != NULL; ++i)
		add_to_argv(_argv, &_argc, config->command[CMD_MNTCHECK][i],
		            vinfo);
	log_argv(_argv);

	if (!spawn_start(_argv, &pid, NULL, &cstdout, NULL, NULL, NULL))
		return -1;

	fp = fdopen(cstdout, "r");
	while (fgets(mte, sizeof(mte), fp) != NULL) {
		/* FIXME: Test it. */
		int (*xcmp)(const char *, const char *);
		const char *fsname, *fstype, *fspt;

		w4rn("mounted filesystem: %s", mte); /* @mte includes '\n' */
		if (!split_bsd_mount(mte, &fsname, &fspt, &fstype)) {
			mounted = -1;
			break;
		}

		/* 
		 * Use case-insensitive for SMB, etc. FIXME: Is it called
		 * "smbfs" under BSD too?
		 */
		xcmp = (fstype != NULL && (strcmp(fstype, "smbfs") == 0 ||
		       strcmp(fstype, "cifs") == 0 ||
		       strcmp(fstype, "ncpfs") == 0)) ? strcasecmp : strcmp;

		/*
		 * FIXME: Does BSD also turn "symlink mountpoints" into "real
		 * mountpoints"?
		 */
		if (xcmp(fsname, dev) == 0 &&
		    strcmp(fspt, vpt->mountpoint) == 0) {
			mounted = 1;
			break;
		}
	}

	fclose(fp); /* automatically closes @cstdout */
	if (waitpid(pid, NULL, 0) != 0)
		l0g("error waiting for child: %s\n", strerror(errno));
	spawn_restore_sigchld();
	return mounted;
}
#else
{
	l0g("check for previous mount not implemented on arch.\n");
	return -1;
}
#endif

/*
 * vol_to_dev
 * @match:
 * @s:
 * @vol:	volume to analyze
 */
static void vol_to_dev(char *match, size_t s, const struct vol *vol)
{
	switch (vol->type) {
	case CMD_SMBMOUNT:
	case CMD_CIFSMOUNT:
		snprintf(match, s, "//%s/%s", vol->server, vol->volume);
		break;

	case CMD_NCPMOUNT:
		snprintf(match, s, "%s/%s", vol->server,
		         kvplist_get(&vol->options, "user"));
		break;

	case CMD_NFSMOUNT:
		snprintf(match, s, "%s:%s", vol->server, vol->volume);
		break;

	case CMD_CRYPTMOUNT: {
		/*
		 * FIXME: ugly hack to support umount.crypt script. I hope that
		 * util-linux will have native dm_crypt support some day.
		 */
		char *wp = match + sizeof_z("/dev/mapper/");
		snprintf(match, s, "/dev/mapper/%s", vol->volume);
		while ((wp = strchr(wp, '/')) != NULL)
			*wp = '_';
		break;
	}

	default:
		strncpy(match, vol->volume, s-1);
		match[s-1] = '\0';
		break;
	}
	return;
}

#if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__APPLE__)
static int split_bsd_mount(char *wp, const char **fsname, const char **fspt,
    const char **fstype)
{
	/*
	 * mntcheck is currently defined as "/bin/mount" in pam_mount.conf.xml
	 * so a line that we read is going to look like
	 * "/dev/ad0s1 on / (ufs, local)".
	 */

	*fsname = wp;
	if ((wp = strchr(wp, ' ')) == NULL) /* parse error */
		return 0;

	/* @wp now at " on ..." */
	*wp++ = '\0';
	if ((wp = strchr(wp, ' ')) == NULL)
		return 0;

	/* wp now at " fspt" */
	*fspt = ++wp;
	if ((wp = strchr(wp, ' ')) == NULL)
		return 0;

	/* wp now at " (fstype, local?, options)" */
	*wp++ = '\0';
	*fstype = ++wp;
	while (isalnum(*wp))
		++wp;
	*wp = '\0';

	return 1;
}
#endif

static void log_pm_input(const struct config *const config,
    const unsigned int vol)
{
	const struct vol *vpt = &config->volume[vol];
	hmc_t *options;

	options = kvplist_to_str(&vpt->options);
	w4rn("information for mount:\n");
	w4rn("----------------------\n");
	w4rn("(defined by %s)\n", vpt->globalconf ? "globalconf" : "luserconf");
	w4rn("user:          %s\n", vpt->user);
	w4rn("server:        %s\n", vpt->server);
	w4rn("volume:        %s\n", vpt->volume);
	w4rn("mountpoint:    %s\n", vpt->mountpoint);
	w4rn("options:       %s\n", options);
	w4rn("fs_key_cipher: %s\n", vpt->fs_key_cipher);
	w4rn("fs_key_path:   %s\n", vpt->fs_key_path);
	w4rn("use_fstab:     %d\n", vpt->use_fstab);
	w4rn("----------------------\n");
	hmc_free(options);
	return;
}

/*
 * mkmountpoint_real - create mountpoint directory
 * @volume:	volume description
 * @d:		directory
 *
 * If the directory @d does not exist, create it and all its parents if
 * @volume->created_mntpt = true. On success, returns true, otherwise false.
 */
static bool mkmountpoint_real(struct vol *const volume, const char *const d)
{
	bool ret = true;
	struct passwd *passwd_ent;
	char dcopy[PATH_MAX + 1], *parent;

	assert(vol_valid(volume));
	assert(d != NULL);

	strncpy(dcopy, d, sizeof_z(dcopy));
	dcopy[sizeof_z(dcopy)] = '\0';
	parent = HX_dirname(dcopy);
	if (!exists(parent) && mkmountpoint(volume, parent) == 0) {
		ret = false;
		goto out;
	}
	if ((passwd_ent = getpwnam(volume->user)) == NULL) {
		l0g("could not determine uid from %s to make %s\n", volume->user, d);
		ret = false;
		goto out;
	}
	/*
	 * The directory will be created in a restricted mode S_IRWXU here.
	 * When mounted, the root directory of the new vfsmount will override
	 * it, so there is no need to use S_IRWXUGO or S_IRWXU | S_IXUGO here.
	 *
	 * Workaround for CIFS on root_squashed NFS: +S_IXUGO
	 */
	if (mkdir(d, S_IRWXU | S_IXUGO) < 0) {
		ret = false;
		goto out;
	}
	if (chown(d, passwd_ent->pw_uid, passwd_ent->pw_gid) < 0) {
		l0g("could not chown %s to %s\n", d, volume->user);
		ret = false;
		goto out;
	}
	volume->created_mntpt = true;
 out:
	free(parent);
	return ret;
}

/*
 * mkmountpoint_pick - create mountpoint for volume
 * @volume:	volume structure
 * @d:		directory to create
 *
 * Switches to the volume user's identity and see if we can create the
 * mountpoint. This is required for NFS mounts with root_squash enabled
 * (assuming the mountpoint's parent is writable by the user, e.g. if it is
 * inside the user's home directory).
 *
 * If that fails, do as usual (create as root, chown to user).
 */
static bool mkmountpoint_pick(struct vol *volume, const char *d)
{
	struct passwd *pe;
	bool ret;

	if ((pe = getpwnam(volume->user)) == NULL) {
		l0g("getpwuid: %s\n", strerror(errno));
		return false;
	}

	w4rn("creating mount point %s\n", d);
	if (seteuid(pe->pw_uid) == 0)
		if (mkmountpoint_real(volume, d))
			return true;

	seteuid(0);
	ret = mkmountpoint_real(volume, d);
	if (!ret)
		l0g("tried to create %s but failed\n", d);
	return ret;
}

/*
 * mkmountpoint -
 *
 * Wrapper for mkmountpoint_pick(). Switch back to root user after
 * mkmountpoint() operation. This is needed, otherwise the PAM stack will
 * (more or less) spuriously fail with PAM_SYSTEM_ERR.
 */
static inline bool mkmountpoint(struct vol *volume, const char *d)
{
	bool r = mkmountpoint_pick(volume, d);
	seteuid(0);
	return r;
}

/*
 * do_unmount
 * @config:	current config
 * @vol:	volume index into @config->vol[]
 * @vinfo:
 * @password:	always %NULL
 *
 * Returns zero on error, positive non-zero for success.
 */
int do_unmount(const struct config *config, const unsigned int vol,
    struct HXbtree *vinfo, const char *const password)
{
	int child_exit, _argc = 0, ret = 1, cstderr = -1;
	pid_t pid = -1;
	const char *_argv[MAX_PAR + 1];
	const struct vol *vpt;
	unsigned int i;
	int type;

	assert(config_valid(config));
	assert(vinfo != NULL);
	assert(password == NULL);	/* password should point to NULL for unmounting */

	vpt = &config->volume[vol];

	if (Debug)
		/*
		 * Often, a process still exists with ~ as its pwd after
		 * logging out.  Running lsof helps debug this.
		 */
		run_lsof(config, vinfo);

	switch (vpt->type) {
		case CMD_SMBMOUNT:
			type = CMD_SMBUMOUNT;
			break;
		case CMD_NCPMOUNT:
			type = CMD_NCPUMOUNT;
			break;
		case CMD_FUSEMOUNT:
			type = CMD_FUSEUMOUNT;
			break;
		case CMD_TRUECRYPTMOUNT:
			type = CMD_TRUECRYPTUMOUNT;
			break;
		default:
			type = CMD_UMOUNT;
			break;
	}

	if (config->command[type][0] == NULL)
		l0g("{smb,ncp}umount not defined in pam_count.conf.xml\n");

	for (i = 0; config->command[type][i] != NULL; ++i)
		add_to_argv(_argv, &_argc, config->command[type][i], vinfo);

	/*
	 * FIXME: ugly hack to support umount.crypt script. I hope that
	 * util-linux will have native dm_crypt support some day.
	 */
	if (vpt->type == CMD_CRYPTMOUNT) {
		_argc = 0;
		add_to_argv(_argv, &_argc, "/sbin/umount.crypt", vinfo);
		add_to_argv(_argv, &_argc, "%(MNTPT)", vinfo);
	}
	log_argv(_argv);
	if (!spawn_start(_argv, &pid, NULL, NULL, &cstderr, set_myuid, NULL)) {
		ret = 0;
		goto out;
	}
	log_output(cstderr, "umount errors:\n");
	w4rn("waiting for umount\n");
	if (waitpid(pid, &child_exit, 0) < 0) {
		l0g("error waiting for child: %s\n", strerror(errno));
		ret = 0;
		goto out;
	} else {
		/* pass on through the result from the umount process */
		ret = !WEXITSTATUS(child_exit);
	}
 out:
	spawn_restore_sigchld();
	if (config->mkmntpoint && config->rmdir_mntpt && vpt->created_mntpt)
		if (rmdir(vpt->mountpoint) < 0)
			/* non-fatal, but warn */
			w4rn("could not remove %s\n", vpt->mountpoint);
	return ret;
}

/* INPUT: fd, a valid file descriptor; buf, a buffer of size count
 * SIDE EFFECTS: buf is written to fd
 * OUTPUT: number of bytes written or 0 on error
 * NOTE: SIGPIPE is ignored during this operation to avoid "broken pipe"
 */
static int pipewrite(int fd, const void *buf, size_t count)
{
	struct sigaction ignoresact = {.sa_handler = SIG_IGN}, oldsact;
	int fnval;

	assert(fd >= 0);
	assert(buf != NULL);
	assert(count >= 0);

	/* avoid bomb on command exiting before data written */
	sigemptyset(&ignoresact.sa_mask);
	if (sigaction(SIGPIPE, &ignoresact, &oldsact) < 0) {
		fnval = -1;
		goto out;
	}
	fnval = write(fd, buf, count);
	/* restore old handler */
	if (sigaction(SIGPIPE, &oldsact, NULL) < 0) {
		fnval = -1;
		goto out;
	}
 out:
	return fnval;
}

static int do_losetup(const struct config *config, const unsigned int vol,
    struct HXbtree *vinfo, const unsigned char *password, size_t password_len)
{
/* PRE:    config points to a valid struct config
 *         config->volume[vol] is a valid struct vol
 *         config->volume[vol].options is valid
 * POST:   volume has associated with a loopback device
 * FN VAL: if error 0 else 1, errors are logged
 */
	pid_t pid;
	int ret = 1, child_exit, _argc = 0, cstdin = -1, cstderr = -1;
	const char *_argv[MAX_PAR + 1];
	const char *cipher, *keybits;
	const struct vol *vpt;
	unsigned int i;

	assert(config_valid(config));
	assert(vinfo != NULL);
	assert(password != NULL);
	/* password_len is unsigned */
	assert(password_len <= MAX_PAR + EVP_MAX_BLOCK_LENGTH);

	vpt     = &config->volume[vol];
	cipher  = kvplist_get(&vpt->options, "encryption");
	keybits = kvplist_get(&vpt->options, "keybits");

	if (config->command[CMD_LOSETUP][0] == NULL) {
		l0g("losetup not defined in pam_mount.conf.xml\n");
		return 0;
	}
	/* FIXME: support OpenBSD */
	/* FIXME: NEW */
	if (cipher != NULL) {
		format_add(vinfo, "CIPHER", cipher);
		if (keybits != NULL)
			format_add(vinfo, "KEYBITS", keybits);
	}
	for (i = 0; config->command[CMD_LOSETUP][i] != NULL; ++i)
		add_to_argv(_argv, &_argc,
		            config->command[CMD_LOSETUP][i], vinfo);

	log_argv(_argv);
	if (!spawn_start(_argv, &pid, &cstdin, NULL, &cstderr, set_myuid, NULL))
		return 0;

	/* note to self: password is decrypted */
	if (pipewrite(cstdin, password, password_len) != password_len) {
	    l0g("error sending password to losetup\n");
	    ret = 0;
	}
	close(cstdin);
	log_output(cstderr, "losetup errors:\n");
	w4rn("waiting for losetup\n");
	if (waitpid(pid, &child_exit, 0) < 0) {
		l0g("error waiting for child: %s\n", strerror(errno));
		ret = 0;
	} else if (ret > 0) {
		/* pass on through the result from the losetup process */
		ret = !WEXITSTATUS(child_exit);
	}
	spawn_restore_sigchld();
	return ret;
}

static int do_unlosetup(const struct config *config, struct HXbtree *vinfo)
{
/* PRE:    config points to a valid struct config
 * POST:   volume has associated with a loopback device
 * FN VAL: if error 0 else 1, errors are logged
 */
	pid_t pid;
	const char *_argv[MAX_PAR + 1];
	int child_exit, _argc = 0;
	unsigned int i;

	assert(config_valid(config));
	assert(vinfo != NULL);

	if (config->command[CMD_UNLOSETUP][0] == NULL) {
		l0g("unlosetup not defined in pam_mount.conf.xml\n");
		return 0;
	}
	/* FIXME: support OpenBSD */
	/* FIXME: NEW */
	for (i = 0; config->command[CMD_UNLOSETUP][i] != NULL; ++i)
		add_to_argv(_argv, &_argc,
		            config->command[CMD_UNLOSETUP][i], vinfo);
	log_argv(_argv);
	if (!spawn_start(_argv, &pid, NULL, NULL, NULL, NULL, NULL))
		return 0;
	w4rn("waiting for losetup delete\n");
	if (waitpid(pid, &child_exit, 0) < 0)
		l0g("error waiting for child: %s\n", strerror(errno));
	spawn_restore_sigchld();
	/* pass on through the result */
	return !WEXITSTATUS(child_exit);
}

static int check_filesystem(const struct config *config, const unsigned int vol,
    struct HXbtree *vinfo, const unsigned char *password, size_t password_len)
{
/* PRE:    config points to a valid struct config
 *         config->volume[vol] is a valid struct vol
 * POST:   integrity of volume has been checked
 * FN VAL: if error 0 else 1, errors are logged
 */
#if defined (__linux__)
	pid_t pid;
	int child_exit, _argc = 0, cstdout = -1, cstderr = -1;
	const char *_argv[MAX_PAR + 1];
	const char *fsck_target;
	const struct vol *vpt;
	unsigned int i;

	assert(config_valid(config));
	assert(vinfo != NULL);
	assert(password != NULL);
	assert(password_len >= 0 &&
	       password_len <= MAX_PAR + EVP_MAX_BLOCK_LENGTH);

	vpt = &config->volume[vol];
	fsck_target = vpt->volume;

	if (config->command[CMD_FSCK][0] == NULL) {
		l0g("fsck not defined in pam_mount.conf.xml\n");
		return 0;
	}

	if (kvplist_contains(&vpt->options, "bind") ||
	    kvplist_contains(&vpt->options, "move") ||
	    fstype_nodev(vpt->fstype) != 0)
		return 1;

	if (kvplist_contains(&vpt->options, "loop")) {
		if (!do_losetup(config, vol, vinfo, password, password_len))
			return 0;
		fsck_target = config->fsckloop;
	} else {
		hmc_t *options = kvplist_to_str(&vpt->options);
		w4rn("volume not a loopback (options: %s)\n", options);
		hmc_free(options);
	}
	/* FIXME: NEW */
	/* FIXME: need to fsck /dev/mapper/whatever... */
	format_add(vinfo, "FSCKTARGET", fsck_target);
	for (i = 0; config->command[CMD_FSCK][i]; ++i)
		add_to_argv(_argv, &_argc, config->command[CMD_FSCK][i], vinfo);

	log_argv(_argv);
	if (!spawn_start(_argv, &pid, NULL, &cstdout, &cstderr, NULL, NULL))
		return 0;

	/* stdout and stderr must be logged for fsck */
	log_output(cstdout, NULL);
	log_output(cstderr, NULL);
	w4rn("waiting for filesystem check\n");
	if (waitpid(pid, &child_exit, 0) < 0)
		l0g("error waiting for child: %s\n", strerror(errno));
	spawn_restore_sigchld();
	if (kvplist_contains(&vpt->options, "loop"))
		if (!do_unlosetup(config, vinfo))
			return 0;
	/*
	 * pass on through the result -- okay if 0 (no errors) or 1 (errors
	 * corrected)
	 */
	return WEXITSTATUS(child_exit) == 0 || WEXITSTATUS(child_exit) == 1;
#else
	l0g("checking filesystem not implemented on arch.\n");
	return 1;
#endif
}

/*
 * do_mount -
 * @config:	current config
 * @vol:	volume index into @config->vol[]
 * @vinfo:
 * @password:
 *
 * Returns zero on error, positive non-zero for success.
 */
int do_mount(const struct config *config, const unsigned int vol,
    struct HXbtree *vinfo, const char *password)
{
	const char *_argv[MAX_PAR + 1];
	size_t _password_len;
	unsigned char _password[MAX_PAR + EVP_MAX_BLOCK_LENGTH];
	int _argc = 0, child_exit = 0, cstdin = -1, cstderr = -1;
	char *mount_user;
	pid_t pid = -1;
	struct vol *vpt;
	unsigned int i;
	int ret;

	assert(config_valid(config));
	assert(vinfo != NULL);
	assert(password != NULL);

	vpt = &config->volume[vol];
	ret = already_mounted(config, vol, vinfo);
	if (ret == -1) {
		l0g("could not determine if %s is already mounted, "
		    "failing\n", vpt->volume);
		return 0;
	} else if (ret == 1) {
		w4rn("%s already seems to be mounted at %s, "
		     "skipping\n", vpt->volume, vpt->mountpoint);
		return 1;
	}
	if (!exists(vpt->mountpoint)) {
		if (config->mkmntpoint) {
			if (!mkmountpoint(vpt, vpt->mountpoint))
				return 0;
		} else {
			l0g("mount point %s does not exist (pam_mount not "
			    "configured to make it)\n",
			    vpt->mountpoint);
			return 0;
		}
	}

	if (config->command[vpt->type][0] == NULL) {
		l0g("proper mount command not defined in "
		    "pam_mount.conf.xml\n");
		return 0;
	}
	w4rn("checking for encrypted filesystem key configuration\n");

	/* FIXME: better done elsewhere? */
	password = (password != NULL) ? password : "";
	if (strlen(vpt->fs_key_cipher) > 0) {
		/*
		 * _password is binary data -- no strlen(), strcpy(), etc.!
		 */
		w4rn("decrypting FS key using system auth. token and "
		     "%s\n", vpt->fs_key_cipher);
		/*
		 * vpt->fs_key_path contains real filesystem key.
		 */
		if (!decrypted_key(_password, &_password_len,
		    vpt->fs_key_path, vpt->fs_key_cipher, password))
			return 0;
	} else {
		/*
		 * _password is an ASCII string in this case -- we'll
		 * treat its MAX_PAR + EVP_MAX_BLOCK_LENGTH size as the
		 * standard string MAX_PAR + 1 in this case
		 */
		strncpy(signed_cast(char *, _password), password, MAX_PAR);
		_password[MAX_PAR-1] = '\0';
		_password_len = strlen(password);
	}
	w4rn("about to start building mount command\n");
	/* FIXME: NEW */
	/* FIXME:
	   l0g("volume type (%d) is unknown\n", vpt->type);
	   return 0;
	 */

	if (vpt->uses_ssh)
		for (i = 0; config->command[CMD_FD0SSH][i] != NULL; ++i)
			add_to_argv(_argv, &_argc,
			            config->command[CMD_FD0SSH][i], vinfo);

	for (i = 0; config->command[vpt->type][i] != NULL; ++i)
		add_to_argv(_argv, &_argc,
		            config->command[vpt->type][i], vinfo);

	if (vpt->type == CMD_LCLMOUNT &&
	    !check_filesystem(config, vol, vinfo, _password, _password_len))
		l0g("error checking filesystem but will continue\n");
	/* send password down pipe to mount process */
	if (vpt->type == CMD_SMBMOUNT || vpt->type == CMD_CIFSMOUNT)
		setenv("PASSWD_FD", "0", 1);
	log_argv(_argv);
	mount_user = strcmp(vpt->fstype, "fuse") == 0 ?
	             vpt->user : NULL;
	if (!spawn_start(_argv, &pid, &cstdin, NULL, &cstderr,
	    set_myuid, mount_user))
		return 0;

	if (vpt->type != CMD_NFSMOUNT)
		if (pipewrite(cstdin, _password, _password_len) !=
		    _password_len)
			/* FIXME: clean: returns value of exit below */
			l0g("error sending password to mount\n");
	close(cstdin);

	/* Paranoia? */
	memset(_password, 0, sizeof(_password));
	log_output(cstderr, "mount errors:\n");
	w4rn("waiting for mount\n");
	if (waitpid(pid, &child_exit, 0) < 0) {
		spawn_restore_sigchld();
		l0g("error waiting for child: %s\n", strerror(errno));
		return 0;
	}

	spawn_restore_sigchld();
	if (Debug)
		if (system("df -Ta") < 0)
			;

	/* pass on through the result from the umount process */
	return !WEXITSTATUS(child_exit);
}

/*
 * mount_op -
 * @mnt:	function to execute mount operations (do_mount or do_unmount)
 * @config:
 * @vol:	volume index into @config->volume[]
 * @password:	password string (may be %NULL on unmount)
 *
 * Returns zero on error, positive non-zero for success.
 * Note: Checked by volume_record_sane() and read_volume()
 */
int mount_op(mount_op_fn_t *mnt, const struct config *config,
    const unsigned int vol, const char *password)
{
	int fnval;
	struct HXbtree *vinfo;
	const struct vol *vpt;
	struct passwd *pe;
	hmc_t *options;

	assert(config_valid(config));

	vpt = &config->volume[vol];

	vinfo = HXformat_init();
	format_add(vinfo, "MNTPT",    vpt->mountpoint);
	format_add(vinfo, "FSCKLOOP", config->fsckloop);
	format_add(vinfo, "FSTYPE",   vpt->fstype);
	format_add(vinfo, "VOLUME",   vpt->volume);
	format_add(vinfo, "SERVER",   vpt->server);
	format_add(vinfo, "USER",     vpt->user);
	misc_add_ntdom(vinfo, vpt->user);

	if ((pe = getpwnam(vpt->user)) == NULL) {
		w4rn("getpwnam(\"%s\") failed: %s\n",
		     Config.user, strerror(errno));
	} else {
		unsigned int uid = pe->pw_uid, gid = pe->pw_gid;
		HXformat_add(vinfo, "USERUID", &uid, HXTYPE_UINT);
		HXformat_add(vinfo, "USERGID", &gid, HXTYPE_UINT);
	}

	/* FIXME: should others remain undefined if == ""? */
	options = kvplist_to_str(&vpt->options);
	format_add(vinfo, "OPTIONS", options);

	if (Debug)
		log_pm_input(config, vol);

	fnval = (*mnt)(config, vol, vinfo, password);
	hmc_free(options);
	HXformat_free(vinfo);
	return fnval;
}

/*
 * fstype_nodev -
 * @name:	fstype to check
 *
 * Returns 1 if the filesystem does not require a block device, 0 if it does
 * require a block device, -1 if we could not find out.
 */
static int fstype_nodev(const char *name) {
	char buf[MAX_PAR];
	FILE *fp;

	if ((fp = fopen("/proc/filesystems", "r")) == NULL)
		return -1;

	while (fgets(buf, sizeof(buf), fp) != NULL) {
		char *bp = buf;
		HX_chomp(buf);
		while (isalpha(*bp)) ++bp;
		while (isspace(*bp)) ++bp;
		if (strcasecmp(bp, name) == 0) {
			fclose(fp);
			return strncasecmp(buf, "nodev", 5) == 0;
		}
	}

	fclose(fp);
	return -1;
}

#ifdef HAVE_STRUCT_LOOP_INFO64_LO_FILE_NAME
/*
 * loop_bk -
 * @filename:	block device to query
 * @i:		pointer to result storage
 *
 * Run the LOOP_GET_STATUS64 ioctl on @filename and store the result in @i.
 * Returns the underlying file of the loop device, or @filename if @filename
 * does not seem to be a loop device at all.
 */
static inline const char *loop_bk(const char *filename, struct loop_info64 *i)
{
	int fd;
	if ((fd = open(filename, O_RDONLY)) < 0)
		return filename;

	if (ioctl(fd, LOOP_GET_STATUS64, i) != 0) {
		close(fd);
		return filename;
	}
	close(fd);
	return signed_cast(char *, i->lo_file_name);
}
#endif

//=============================================================================
