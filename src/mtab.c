/*
 *	Copyright © Jan Engelhardt, 2009
 *
 *	This file is part of pam_mount; you can redistribute it and/or
 *	modify it under the terms of the GNU Lesser General Public License
 *	as published by the Free Software Foundation; either version 2.1
 *	of the License, or (at your option) any later version.
 */

/*
 * We need a way to store the
 * container<->loop device<->crypto device<->mountpoint associations,
 * because it is a real pain to try and retrieve them through the
 * interfaces of each layer. Especially because given a loop device,
 * we have (had) no knowledge of whether the user fed to loop device
 * to cryptsetup (possibly via mount.crypt) or whether mount.crypt
 * set up the loop device itself.
 *
 * We used to write {container, mountpoint} into /etc/mtab, but only
 * informationally, since /etc/mtab could be a link to the read-only
 * /proc/mounts.
 *
 * In fact, this is always the case on Solaris -- /etc/mnttab is read-only.
 * BSD does not even have an mtab.
 * So we do need a way to track our device associations.
 */
#define _GNU_SOURCE 1
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <libHX/ctype_helper.h>
#include <libHX/defs.h>
#include <libHX/string.h>
#include "pam_mount.h"

/* crypto mtab */
static const char pmt_cmtab_file[] = "/etc/cmtab";
#if defined(__linux__)
static const char pmt_smtab_file[] = "/etc/mtab";
/*
 * It does not make sense to add path names for OSes that only
 * have a read-only smtab (system mtab). Hence only __linux__.
 */
#else
static const char pmt_smtab_file[] = "";
#endif

/**
 * mt_esccat - escape string as needed and append to buffer
 * @vp:		buffer to append to
 * @str:	string to escape
 */
static void mt_esccat(hxmc_t **vp, const char *str)
{
	static const char del[] = " \\\t\n";
	char esc[5] = "\\000";
	const char *p;
	size_t seg;

	if (strpbrk(str, del) == NULL) {
		HXmc_strcat(vp, str);
		return;
	}

	for (p = str; *p != '\0'; ++p) {
		seg = strcspn(str, del);
		HXmc_memcat(vp, p, seg);
		p += seg;
		if (*p == '\0')
			break;
		esc[1] = '0' + ((*p & 0700) >> 6);
		esc[2] = '0' + ((*p & 0070) >> 3);
		esc[3] = '0' + (*p & 0007);
		HXmc_strcat(vp, esc);
	}
}

/**
 * mt_unescape - unescape mtab data
 * @input:	input string, will be modified in-place
 */
static char *mt_unescape(char *input)
{
	unsigned char c;
	char *input_orig, *ptr, *output;
	unsigned int delta;

	if ((ptr = strchr(input, '\\')) == NULL)
		return input;

	input_orig = input;
	for (output = input = ptr; *input != '\0'; ) {
		if (!HX_isdigit(input[1]) || !HX_isdigit(input[2]) ||
		    !HX_isdigit(input[3])) {
			++input;
			continue;
		}

		c  = ((input[1] - '0') & 07) << 6;
		c |= ((input[2] - '0') & 07) << 3;
		c |= (input[3] - '0') & 07;
		*output++ = c;
		input += 4;

		ptr = strchr(input, '\\');
		if (ptr == NULL)
			ptr = input + strlen(input);
		delta = ptr - input;
		memmove(output, input, delta);
		input  += delta;
		output += delta;
	}
	*output++ = '\0';
	return input_orig;
}

static int pmt_mtab_add(const char *file, const char *line)
{
	int fd, ret;

	if ((fd = open(file, O_RDWR | O_CREAT | O_APPEND,
	    S_IRUGO | S_IWUSR)) < 0)
		return -errno;

	ret = fcntl(fd, F_SETLKW, &(struct flock){.l_type = F_WRLCK,
	      	.l_whence = SEEK_SET, .l_start = 0, .l_len = 0});
	if (ret < 0) {
		ret = errno;
		close(fd);
		return -(errno = ret);
	}

	if ((ret = write(fd, line, strlen(line))) < 0)
		ret = -errno;
	else if (ret < strlen(line))
		ret = 0;
	close(fd);
	return ret;
}

int pmt_smtab_add(const char *device, const char *mountpoint,
    const char *fstype, const char *options)
{
	int ret;
	hxmc_t *line = HXmc_meminit(NULL, strlen(device) +
		strlen(mountpoint) + strlen(fstype) + strlen(options) + 8);
	if (line == NULL)
		return -errno;

	mt_esccat(&line, device);
	HXmc_strcat(&line, " ");
	mt_esccat(&line, mountpoint);
	HXmc_strcat(&line, " ");
	mt_esccat(&line, fstype);
	HXmc_strcat(&line, " ");
	mt_esccat(&line, options);
	HXmc_strcat(&line, " 0 0\n");
	ret = pmt_mtab_add(pmt_smtab_file, line);
	HXmc_free(line);
	return ret;
}

int pmt_cmtab_add(const char *mountpoint, const char *container,
    const char *loop_device, const char *crypto_device)
{
	hxmc_t *line;
	int ret;

	if (container == NULL)
		return -EINVAL;
	if (loop_device == NULL)
		loop_device = "-";
	if (crypto_device == NULL)
		crypto_device = "-";

	/* Preallocate just the normal size */
	line = HXmc_meminit(NULL, strlen(mountpoint) + strlen(container) +
	       strlen(loop_device) + strlen(crypto_device) + 5);
	if (line == NULL)
		return -errno;

	mt_esccat(&line, mountpoint);
	HXmc_strcat(&line, "\t");
	mt_esccat(&line, container);
	HXmc_strcat(&line, "\t");
	mt_esccat(&line, loop_device);
	HXmc_strcat(&line, "\t");
	mt_esccat(&line, crypto_device);
	HXmc_strcat(&line, "\n");
	ret = pmt_mtab_add(pmt_cmtab_file, line);
	HXmc_free(line);
	return ret;
}

/**
 * cmtab_parse_line - parse one line from a cmtab file into its fields
 * @line:	line buffer, will be modified in-place
 * @field:	field pointers
 */
static void cmtab_parse_line(char *line, char **field)
{
	static const unsigned int fld_num = 4;
	const char *eol = line + strlen(line);
	char *p = line, *q;
	int i;

	for (i = 0; i < fld_num; ++i)
		field[i] = NULL;

	for (i = 0; i < fld_num && p < eol; ++i) {
		for (q = p; HX_isspace(*q); ++q)
			;
		for (p = q; !HX_isspace(*p) && *p != '\0'; ++p)
			;
		*p++ = '\0';
		field[i] = mt_unescape(q);
	}
}

/**
 * cmtab_get - get one mtab entry
 * @spec:		specificator to match on (must be %CMTABF_*)
 * @type:		type of the specificator
 * @mountpoint:		mountpoint
 * @container:		container file or block device
 * @loop_device:	loop device (if any)
 * @crypto_device:	crypto device (full path)
 */
int pmt_cmtab_get(const char *spec, enum cmtab_field type, char **mountpoint,
    char **container, char **loop_device, char **crypto_device)
{
	hxmc_t *line = NULL;
	FILE *fp;
	int ret = 0;

	if (type >= __CMTABF_MAX)
		return -EINVAL;
	if (mountpoint    != NULL) *mountpoint    = NULL;
	if (container     != NULL) *container     = NULL;
	if (loop_device   != NULL) *loop_device   = NULL;
	if (crypto_device != NULL) *crypto_device = NULL;

	if ((fp = fopen(pmt_cmtab_file, "r")) == NULL)
		return -errno;

	fcntl(fileno(fp), F_SETLKW, &(struct flock){.l_type = F_RDLCK,
		.l_whence = SEEK_SET, .l_start = 0, .l_len = 0});

	while (HX_getl(&line, fp) != NULL) {
		char *field[4];

		cmtab_parse_line(line, field);
		if (strcmp(spec, field[type]) != 0)
			continue;

		if (mountpoint != NULL) {
			free(*mountpoint);
			*mountpoint = HX_strdup(field[0]);
		}
		if (container != NULL) {
			free(*container);
			*container = HX_strdup(field[1]);
		}
		if (loop_device != NULL && strcmp(field[2], "-") != 0) {
			free(*loop_device);
			*loop_device = HX_strdup(field[2]);
		}
		if (crypto_device != NULL && strcmp(field[3], "-") != 0) {
			free(*crypto_device);
			*crypto_device = HX_strdup(field[3]);
		}
		ret = 1;
		/*
		 * most recent entry is at the bottom - must continue to
		 * loop in case of overmounts.
		 */
 	}

	HXmc_free(line);
	fclose(fp);
	return ret;
}

/**
 * pmt_mtab_remove - remove entry from mtab-style file
 * @file:	file to inspect and modify
 * @spec:	string to match on
 * @field_idx:	field to match on
 */
static int pmt_mtab_remove(const char *file, const char *spec,
    unsigned int field_idx)
{
	hxmc_t *line = NULL;
	size_t pos_src = 0, pos_dst = 0;
	FILE *fp;
	int ret;

	if ((fp = fopen(file, "r+")) == NULL)
		return -errno;

	ret = fcntl(fileno(fp), F_SETLKW, &(struct flock){.l_type = F_WRLCK,
	      	.l_whence = SEEK_SET, .l_start = 0, .l_len = 0});
	if (ret < 0) {
		ret = -errno;
		goto out;
	}

	ret = 0;
	do {
		char *field[4];
		size_t curr_pos;

		curr_pos = ftello(fp);
		if (HX_getl(&line, fp) == NULL)
			break;
		cmtab_parse_line(line, field);
		if (strcmp(spec, field[field_idx]) != 0)
			continue;
		pos_src = ftello(fp);
		pos_dst = curr_pos;
		ret = 1;
		/* continue looping - and look for overmounts */
	} while (true);

	if (ret == 1) {
		char buf[1024];
		ssize_t rdret, wrret;

		while ((rdret = pread(fileno(fp), buf, sizeof(buf), pos_src)) > 0) {
			wrret = pwrite(fileno(fp), buf, rdret, pos_dst);
			if (wrret != rdret) {
				w4rn("%s: pwrite: %s\n", __func__, strerror(errno));
				if (wrret > 0)
					pos_dst += wrret;
				break;
			}
			pos_src += rdret;
			pos_dst += rdret;
		}

		if (ftruncate(fileno(fp), pos_dst) < 0)
			w4rn("%s: ftruncate: %s\n", __func__, strerror(errno));
	}

 out:
	HXmc_free(line);
	fclose(fp);
	return ret;
}

/**
 * pmt_smtab_remove - remove an smtab entry
 * @spec:	specificator to match on (must be %SMTABF_*)
 * @type:	type of the specificator
 */
int pmt_smtab_remove(const char *spec, enum smtab_field type)
{
	if (type >= __SMTABF_MAX)
		return -EINVAL;
	if (*pmt_smtab_file != '\0')
		return pmt_mtab_remove(pmt_smtab_file, spec, type);
	return 0;
}

/**
 * pmt_cmtab_remove - remove a cmtab entry
 * @spec:	specificator to match on (must be %CMTABF_*)
 * @type:	type of the specificator
 *
 * By definition, removal operates on the most recent entry in an mtab.
 */
int pmt_cmtab_remove(const char *spec, enum cmtab_field type)
{
	if (type >= __CMTABF_MAX)
		return -EINVAL;
	return pmt_mtab_remove(pmt_cmtab_file, spec, type);
}