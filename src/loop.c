/*
 *	Copyright © Jan Engelhardt, 2008
 *
 *	This file is part of pam_mount; you can redistribute it and/or
 *	modify it under the terms of the GNU Lesser General Public License
 *	as published by the Free Software Foundation; either version 2.1
 *	of the License, or (at your option) any later version.
 */
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <libHX/ctype_helper.h>
#include <libHX/defs.h>
#include <libHX/proc.h>
#include <libHX/string.h>
#include <openssl/evp.h>
#include "pam_mount.h"
#include "config.h"

/**
 * @lower_device:	path to container, if it is a block device, otherwise
 * 			path to a loop device translating it into a bdev
 * @crypto_device:	same as @crypto_name, but full path (/dev/mapper/X)
 * @crypto_name:	crypto device name we chose
 * @cipher:		cipher to use with cryptsetup
 * @hash:		hash to use with cryptsetup
 * @keysize:		and the keysize
 * @fskey:		the actual filesystem key
 * @fskey_size:		fskey size, in bytes
 */
struct ehdmount_ctl {
	char *lower_device;
	hxmc_t *crypto_name, *crypto_device;
	const char *cipher, *hash;
	const unsigned char *fskey;
	unsigned int fskey_size;
	bool blkdev, readonly;
};

/**
 * pmt_loop_file_name -
 * @filename:	block device to query
 * @i:		pointer to result storage
 *
 * Run the LOOP_GET_STATUS64 ioctl on @filename and store the result in @i.
 * Returns the underlying file of the loop device, or @filename if @filename
 * does not seem to be a loop device at all.
 */
#ifdef HAVE_STRUCT_LOOP_INFO64_LO_FILE_NAME
	/* elsewhere */
#else
const char *pmt_loop_file_name(const char *filename, struct loop_info64 *i)
{
	return filename;
}
#endif

/**
 * pmt_loop_setup - associate file to a loop device
 * @filename:	file to associate
 * @result:	result buffer for path to loop device
 * @ro:		readonly
 *
 * Returns -errno on error, or positive on success,
 * zero when no devices were available.
 */
#if defined(HAVE_STRUCT_LOOP_INFO64_LO_FILE_NAME) || \
    defined(HAVE_DEV_VNDVAR_H)
	/* elsewhere */
#else
int pmt_loop_setup(const char *filename, char **result, bool ro)
{
	fprintf(stderr, "%s: no pam_mount support for loop devices "
	        "on this platform\n", __func__);
	return -ENOSYS;
}
#endif

/**
 * pmt_loop_release - release a loop device
 * @device:	loop node
 */
#if defined(HAVE_STRUCT_LOOP_INFO64_LO_FILE_NAME) || \
    defined(HAVE_DEV_VNDVAR_H)
	/* elsewhere */
#else
int pmt_loop_release(const char *device)
{
	return -ENOSYS;
}
#endif

/**
 * ehd_is_luks - check if @path points to a LUKS volume (cf. normal dm-crypt)
 * @path:	path to the crypto container
 * @blkdev:	path is definitely a block device
 */
int ehd_is_luks(const char *path, bool blkdev)
{
	const char *lukscheck_args[] = {
		"cryptsetup", "isLuks", path, NULL,
	};
	char *loop_device;
	int ret;

	if (!blkdev) {
		ret = pmt_loop_setup(path, &loop_device, true);
		if (ret == 0) {
			fprintf(stderr, "No free loop device\n");
			return -1;
		} else if (ret < 0) {
			fprintf(stderr, "%s: could not set up loop device: %s\n",
			        __func__, strerror(-ret));
			return -1;
		}
		lukscheck_args[2] = loop_device;
	}

	if ((ret = HXproc_run_sync(lukscheck_args, HXPROC_VERBOSE)) < 0)
		fprintf(stderr, "run_sync: %s\n", strerror(-ret));
	else if (ret > 0xFFFF)
		/* terminated */
		ret = -1;
	else
		/* exited, and we need success or fail */
		ret = ret == 0;

	if (!blkdev)
		pmt_loop_release(loop_device);
	return ret;
}

/**
 * ehd_load_2 - set up dm-crypt device
 */
static bool ehd_load_2(struct ehdmount_ctl *ctl)
{
	int ret;
	bool is_luks = false;
	const char *start_args[11];
	struct HXproc proc;

	ret = ehd_is_luks(ctl->lower_device, true);
	if (ret >= 0) {
		int argk = 0;

		is_luks = ret == 1;
		start_args[argk++] = "cryptsetup";
		if (ctl->readonly)
			start_args[argk++] = "--readonly";
		if (ctl->cipher != NULL) {
			start_args[argk++] = "-c";
			start_args[argk++] = ctl->cipher;
		}
		if (is_luks) {
			start_args[argk++] = "luksOpen";
			start_args[argk++] = ctl->lower_device;
			start_args[argk++] = ctl->crypto_name;
		} else {
			start_args[argk++] = "--key-file=-";
			start_args[argk++] = "-h";
			start_args[argk++] = ctl->hash;
			start_args[argk++] = "create";
			start_args[argk++] = ctl->crypto_name;
			start_args[argk++] = ctl->lower_device;
		}
		start_args[argk] = NULL;
		assert(argk < ARRAY_SIZE(start_args));
	} else {
		l0g("cryptsetup isLuks got terminated\n");
		return false;
	}

	if (Debug)
		arglist_llog(start_args);

	memset(&proc, 0, sizeof(proc));
	proc.p_flags = HXPROC_VERBOSE | HXPROC_STDIN;
	if ((ret = HXproc_run_async(start_args, &proc)) <= 0) {
		l0g("Error setting up crypto device: %s\n", strerror(-ret));
		return false;
	}

	/* Ignore return value, we can't do much in case it fails */
	if (write(proc.p_stdin, ctl->fskey, ctl->fskey_size) < 0)
		w4rn("%s: password send erro: %s\n", __func__, strerror(errno));
	close(proc.p_stdin);
	if ((ret = HXproc_wait(&proc)) != 0) {
		w4rn("cryptsetup exited with non-zero status %d\n", ret);
		return false;
	}

	return true;
}

static hxmc_t *ehd_crypto_name(const char *s)
{
	hxmc_t *ret;
	char *p;

	ret = HXmc_strinit(s);
	for (p = ret; *p != '\0'; ++p)
		if (!HX_isalnum(*p))
			*p = '_';
	return ret;
}

/**
 * ehd_load - set up crypto device for an EHD container
 * @cont_path:		path to the container
 * @crypto_device:	store crypto device here
 * @cipher:		filesystem cipher
 * @hash:		hash function for cryptsetup (default: plain)
 * @fskey:		unencrypted fskey data (not path)
 * @fskey_size:		size of @fskey, in bytes
 * @readonly:		set up loop device as readonly
 */
int ehd_load(const char *cont_path, hxmc_t **crypto_device_pptr,
    const char *cipher, const char *hash, const unsigned char *fskey,
    unsigned int fskey_size, bool readonly)
{
	struct ehdmount_ctl ctl = {
		.cipher     = cipher,
		.hash       = hash ? : "plain",
		.fskey      = fskey,
		.fskey_size = fskey_size,
		.readonly   = readonly,
	};
	struct stat sb;
	int ret;

	if (stat(cont_path, &sb) < 0) {
		l0g("Could not stat %s: %s\n", cont_path, strerror(errno));
		return -errno;
	}

	if (S_ISBLK(sb.st_mode)) {
		ctl.blkdev       = true;
		ctl.lower_device = const_cast1(char *, cont_path);
	} else {
		/* need losetup since cryptsetup needs block device */
		w4rn("Setting up loop device for file %s\n", cont_path);
		ret = pmt_loop_setup(cont_path, &ctl.lower_device, readonly);
		if (ret == 0) {
			l0g("Error: no free loop devices\n");
			return false;
		} else if (ret < 0) {
			l0g("Error setting up loopback device for %s: %s\n",
			    cont_path, strerror(-ret));
			return false;
		} else {
			w4rn("Using %s\n", ctl.lower_device);
		}
	}

	ctl.crypto_name = ehd_crypto_name(cont_path);
	w4rn("Using %s as dmdevice name\n", ctl.crypto_name);
	ctl.crypto_device = HXmc_strinit("/dev/mapper/");
	HXmc_strcat(&ctl.crypto_device, ctl.crypto_name);

	ret = ehd_load_2(&ctl);
	if (!ret)
		crypto_device_pptr = NULL;
	if (ctl.lower_device != cont_path) {
		pmt_loop_release(ctl.lower_device);
		free(ctl.lower_device);
	}
	if (crypto_device_pptr != NULL)
		*crypto_device_pptr = ctl.crypto_device;
	else
		HXmc_free(ctl.crypto_device);
	HXmc_free(ctl.crypto_name);
	return ret;
}

/**
 * ehd_unload_crypto - deactivate crypto device
 * @crypto_device:	full path to the crypto device (/dev/mapper/X)
 */
static bool ehd_unload_crypto(const char *crypto_device)
{
	const char *crypto_name = HX_basename(crypto_device);
	const char *const args[] = {
		"cryptsetup", "remove", crypto_name, NULL,
	};
	int ret;

	if ((ret = HXproc_run_sync(args, HXPROC_VERBOSE)) == 0)
		return true;

	l0g("Could not unload dm-crypt device \"%s\" (%s), "
	    "cryptsetup %s with run_sync status %d\n",
	    crypto_name, crypto_device, ret);
	return false;
}

/**
 * ehd_unload -
 * @crypto_device:	dm-crypt device (/dev/mapper/X)
 * @only_crypto:	do not unload any lower device
 *
 * Determines the underlying device of the crypto target. Unloads the crypto
 * device, and then the loop device if one is used.
 *
 * Using the external cryptsetup program because the cryptsetup C API does
 * not look as easy as the loop one, and does not look shared (i.e. available
 * as a system library) either.
 */
int ehd_unload(const char *crypto_device, bool only_crypto)
{
	const char *const args[] = {
		"cryptsetup", "status", HX_basename(crypto_device), NULL,
	};
	const char *lower_device = NULL;
	hxmc_t *line = NULL;
	bool f_ret = false;
	FILE *fp = NULL;
	struct HXproc proc = {.p_flags = HXPROC_VERBOSE | HXPROC_STDOUT};
	int ret;

	if ((ret = HXproc_run_async(args, &proc)) <= 0) {
		l0g("%s: could not run %s: %s\n",
		    __func__, *args, strerror(-ret));
		return ret;
	}

	fp = fdopen(proc.p_stdout, "r");
	if (fp == NULL)
		goto out;

	while (HX_getl(&line, fp) != NULL) {
		const char *p = line;
		HX_chomp(line);

		while (HX_isspace(*p))
			++p;
		if (strncmp(p, "device:", strlen("device:")) != 0)
			continue;
		while (!HX_isspace(*p) && *p != '\0')
			++p;
		/*
		 * Relying on the fact that dmcrypt does not
		 * allow spaces or newlines in filenames.
		 */
		while (HX_isspace(*p))
			++p;
		lower_device = p;
		break;
	}

	if (!ehd_unload_crypto(crypto_device))
		goto out;
	if (!only_crypto) {
		ret = pmt_loop_release(lower_device);
		/*
		 * Success, not-assigned (ENXIO) or not-a-loop-device (ENOTTY)
		 * shall pass.
		 */
		if (ret <= 0 && ret != -ENXIO && ret != -ENOTTY)
			goto out;
	}

	f_ret = true;
 out:
	if (fp != NULL)
		fclose(fp);
	else
		close(proc.p_stdout);
	HXproc_wait(&proc);
	return f_ret;
}

struct decrypt_info {
	const char *keyfile;
	hxmc_t *password;
	const EVP_CIPHER *cipher;
	const EVP_MD *digest;

	const unsigned char *data;
	unsigned int keysize;

	const unsigned char *salt;
};

static hxmc_t *ehd_decrypt_key2(const struct decrypt_info *info)
{
	unsigned char key[EVP_MAX_KEY_LENGTH], iv[EVP_MAX_IV_LENGTH];
	unsigned int out_cumul_len = 0;
	EVP_CIPHER_CTX ctx;
	int out_len = 0;
	hxmc_t *out;

	if (EVP_BytesToKey(info->cipher, info->digest, info->salt,
	    signed_cast(const unsigned char *, info->password),
	    (info->password == NULL) ? 0 : HXmc_length(info->password),
	    1, key, iv) <= 0) {
		l0g("EVP_BytesToKey failed\n");
		return false;
	}

	out = HXmc_meminit(NULL, info->keysize + info->cipher->block_size);
	EVP_CIPHER_CTX_init(&ctx);
	EVP_DecryptInit_ex(&ctx, info->cipher, NULL, key, iv);
	EVP_DecryptUpdate(&ctx, signed_cast(unsigned char *,
		&out[out_len]), &out_len, info->data, info->keysize);
	out_cumul_len += out_len;
	EVP_DecryptFinal_ex(&ctx, signed_cast(unsigned char *,
		&out[out_len]), &out_len);
	out_cumul_len += out_len;
	HXmc_memcat(&out, out, out_cumul_len);
	EVP_CIPHER_CTX_cleanup(&ctx);

	return out;
}

hxmc_t *ehd_decrypt_key(const char *keyfile, const char *digest_name,
    const char *cipher_name, hxmc_t *password)
{
	struct decrypt_info info = {
		.keyfile  = keyfile,
		.digest   = EVP_get_digestbyname(digest_name),
		.cipher   = EVP_get_cipherbyname(cipher_name),
		.password = password,
	};
	hxmc_t *f_ret = NULL;
	unsigned char *buf;
	struct stat sb;
	ssize_t i_ret;
	int fd;

	if (info.digest == NULL) {
		l0g("Unknown digest: %s\n", digest_name);
		return false;
	}
	if (info.cipher == NULL) {
		l0g("Unknown cipher: %s\n", cipher_name);
		return false;
	}
	if ((fd = open(info.keyfile, O_RDONLY)) < 0) {
		l0g("Could not open %s: %s\n", info.keyfile, strerror(errno));
		return false;
	}
	if (fstat(fd, &sb) < 0) {
		l0g("stat: %s\n", strerror(errno));
		goto out;
	}

	if ((buf = xmalloc(sb.st_size)) == NULL)
		return false;

	if ((i_ret = read(fd, buf, sb.st_size)) != sb.st_size) {
		l0g("Incomplete read of %u bytes got %Zd bytes\n",
		    sb.st_size, i_ret);
		goto out2;
	}

	info.salt    = &buf[strlen("Salted__")];
	info.data    = info.salt + PKCS5_SALT_LEN;
	info.keysize = sb.st_size - (info.data - buf);
	f_ret = ehd_decrypt_key2(&info);

 out2:
	free(buf);
 out:
	close(fd);
	return f_ret;
}

static unsigned int __cipher_digest_security(const char *s)
{
	static const char *const blacklist[] = {
		"ecb",
		"rc2", "rc4", "des", "des3",
		"md2", "md4",
	};
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(blacklist); ++i)
		if (strcmp(s, blacklist[i]) == 0)
			return 0;

	return 2;
}

/**
 * cipher_digest_security - returns the secure level of a cipher/digest
 * @s:	name of the cipher or digest specification
 * 	(can either be OpenSSL or cryptsetup name)
 *
 * Returns 0 if it is considered insecure, 1 if I would have a bad feeling
 * using it, and 2 if it is appropriate.
 */
unsigned int cipher_digest_security(const char *s)
{
	char *base, *tmp, *wp;
	unsigned int ret;

	if ((base = xstrdup(s)) == NULL)
		return 2;

	tmp = base;
	while ((wp = HX_strsep(&tmp, ",-.:_")) != NULL)
		if ((ret = __cipher_digest_security(wp)) < 2)
			break;

	free(base);
	return ret;
}

static struct {
	struct sigaction oldact;
	bool echo;
	int fd;
} pmt_pwq_restore;

static void pmt_password_stop(int s)
{
	struct termios ti;

	if (!pmt_pwq_restore.echo)
		return;
	if (tcgetattr(pmt_pwq_restore.fd, &ti) == 0) {
		ti.c_lflag |= ECHO;
		tcsetattr(pmt_pwq_restore.fd, TCSANOW, &ti);
	}
	sigaction(s, &pmt_pwq_restore.oldact, NULL);
	if (s != 0)
		kill(0, s);
}

static hxmc_t *__pmt_get_password(FILE *fp)
{
	hxmc_t *ret = NULL;
	memset(&pmt_pwq_restore, 0, sizeof(pmt_pwq_restore));
	pmt_pwq_restore.fd = fileno(fp);

	if (isatty(fileno(fp))) {
		struct sigaction sa;
		struct termios ti;

		if (tcgetattr(fileno(fp), &ti) == 0) {
			pmt_pwq_restore.echo = ti.c_lflag & ECHO;
			if (pmt_pwq_restore.echo) {
				sigemptyset(&sa.sa_mask);
				sa.sa_handler = pmt_password_stop;
				sa.sa_flags   = SA_RESETHAND;
				sigaction(SIGINT, &sa, NULL);
				ti.c_lflag &= ~ECHO;
				tcsetattr(fileno(fp), TCSANOW, &ti);
			}
		}
	}

	if (HX_getl(&ret, fp) != NULL) {
		HX_chomp(ret);
		HXmc_setlen(&ret, strlen(ret));
	}
	pmt_password_stop(0);
	return ret;
}

hxmc_t *pmt_get_password(const char *prompt)
{
	hxmc_t *ret;

	printf("%s", (prompt != NULL) ? prompt : "Password: ");
	fflush(stdout);
	ret = __pmt_get_password(stdin);
	printf("\n");
	return ret;
}
