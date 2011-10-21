#ifndef _CRYPTMOUNT_H
#define _CRYPTMOUNT_H 1

#include <libHX/string.h>
#ifndef __cplusplus
#	include <stdbool.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Mnemonics for functions expecting a "blkdev?" argument, e.g. ehd_is_luks.
 * %EHD_BLKDEV_NEEDED:	the argument may not be a block device
 * %EHD_BLKDEV_ASSURED:	the argument is assured to be a block device
 */
enum {
	EHD_BLKDEV_UNKNOWN = 0,
	EHD_BLKDEV_ASSURED = 1,
};

/**
 * Result codes for ehd_decrypt_keyfile
 * %EHD_DECRYPTKF_SUCCESS:	no error
 * %EHD_DECRYPTKF_NODIGEST:	the digest is not known
 * %EHD_DECRYPTKF_NOCIPHER:	the cipher is not known
 * %EHD_DECRYPTKF_OTHER:	other unspecified error
 */
enum {
	EHD_DECRYPTKF_SUCCESS = 0,
	EHD_DECRYPTKF_NODIGEST,
	EHD_DECRYPTKF_NOCIPHER,
	EHD_DECRYPTKF_OTHER,
};

/**
 * Result codes for ehd_cipherdigest_security
 * (negative indicates system error code).
 *
 * %EHD_SECURITY_BLACKLISTED:	cipher/digest classified as absolute no-go
 * %EHD_SECURITY_SUBPAR:	use of cipher/digest is disrecommended
 * %EHD_SECURITY_UNSPEC:	no verdict
 * %EHD_SECURITY_ADEQUATE:	cipher/digest passes
 *
 * Ordering is supported, x < %EHD_SECURITY_* may be used.
 */
enum {
	EHD_SECURITY_BLACKLISTED = 0,
	EHD_SECURITY_SUBPAR,
	EHD_SECURITY_UNSPEC,
	EHD_SECURITY_ADEQUATE,
};

/**
 * struct ehd_decryptkf_params - parameter agglomerator for ehd_decrypt_keyfile
 * @keyfile:	path to the key file
 * @digest:	digest used for the key file
 * @cipher:	cipher used for the key file
 * @password:	password to unlock the key material
 * @result:	result pointer to unencrypted content
 */
struct ehd_decryptkf_params {
	const char *keyfile, *digest, *cipher, *password;
	hxmc_t *result;
};

/**
 * struct ehd_mount_request - mapping and mount request for EHD
 * @container:		path to disk image
 * @mountpoint:		where to mount the volume on
 * @readonly:		whether to create a readonly vfsmount
 * @key_data:		key material/password
 * @key_size:		size of key data, in bytes
 * @fs_cipher:		cipher used for filesystem, if any. (cryptsetup name)
 * @fs_hash:		hash used for filesystem, if any. (cryptsetup name)
 * @trunc_keysize:	extra cryptsetup instruction for truncation (in bytes)
 */
struct ehd_mount_request {
	const char *container;
	const char *mountpoint;
	const char *fs_cipher, *fs_hash;
	const void *key_data;
	unsigned int key_size, trunc_keysize;
	bool readonly;
};

struct ehd_mount_info;

extern int cryptmount_init(void);
extern void cryptmount_exit(void);

extern int ehd_load(const struct ehd_mount_request *, struct ehd_mount_info *);
extern int ehd_unload(const struct ehd_mount_info *);
extern void ehd_mountinfo_free(struct ehd_mount_info *);
extern int ehd_is_luks(const char *, bool);

extern hxmc_t *ehd_decrypt_keyfile(struct ehd_decryptkf_params *);
extern const char *ehd_decryptkf_strerror(int);

extern int ehd_cipherdigest_security(const char *);
extern hxmc_t *ehd_get_password(const char *);

/*
 *	loop.c
 */
enum {
	EHD_LOSETUP_RW = 0,
	EHD_LOSETUP_RO = 1,
};

extern int ehd_loop_setup(const char *, char **, bool);
extern int ehd_loop_release(const char *);


#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* _CRYPTMOUNT_H */
