#ifndef _CMT_INTERNAL_H
#define _CMT_INTERNAL_H 1

#include <stdbool.h>
#include <libHX/string.h>

/**
 * struct ehd_mount - EHD mount info
 * @container:		path to disk image
 * @lower_device:	link to either @container if a block device,
 * 			otherwise points to @loop_device.
 * @loop_device:	loop device that was created, if any
 * @crypto_name:	crypto device that was created (basename only)
 * @crypto_device:	full path to the crypto device
 */
struct ehd_mount_info {
	char *container;
	const char *lower_device;
	char *loop_device;
	hxmc_t *crypto_name;
	hxmc_t *crypto_device;
};

/**
 * struct ehd_mount_request - mapping and mount request for EHD
 * @container:		path to disk image
 * @mountpoint:		where to mount the volume on
 * @fs_cipher:		cipher used for filesystem, if any. (cryptsetup name)
 * @fs_hash:		hash used for filesystem, if any. (cryptsetup name)
 * @key_data:		key material/password
 * @key_size:		size of key data, in bytes
 * @trunc_keysize:	extra cryptsetup instruction for truncation (in bytes)
 * @readonly:		whether to create a readonly vfsmount
 */
struct ehd_mount_request {
	char *container, *crypto_name, *mountpoint;
	char *fs_cipher, *fs_hash;
	void *key_data;
	unsigned int key_size, trunc_keysize;
	bool readonly;
};

struct ehd_crypto_ops {
	int (*load)(const struct ehd_mount_request *, struct ehd_mount_info *);
	int (*unload)(const struct ehd_mount_info *);
};

extern const struct ehd_crypto_ops ehd_cgd_ops;
extern const struct ehd_crypto_ops ehd_dmcrypt_ops;

#endif /* _CMT_INTERNAL_H */
