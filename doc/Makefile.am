# -*- Makefile -*-

man_MANS = pam_mount.8 pam_mount.conf.5
dist_man_MANS = mount.crypt.8 mount.crypt_LUKS.8 mount.crypto_LUKS.8 \
		pmvarrun.8 pmt-ehd.8 \
		umount.crypt.8 umount.crypt_LUKS.8 \
		umount.crypto_LUKS.8
EXTRA_DIST = bugs.txt faq.txt install.txt news.txt options.txt todo.txt \
	pam_mount.8.in pam_mount.conf.5.in
CLEANFILES = ${man_MANS}

replcmd = perl -lpe 's/\@''PACKAGE\@/@PACKAGE@/g;s/\@''PACKAGE_VERSION\@/@PACKAGE_VERSION@/g;s/\@''PACKAGE_RELDATE\@/@PACKAGE_RELDATE@/g;'

pam_mount.8: pam_mount.8.in ${top_builddir}/config.status
	${AM_V_GEN}${replcmd} ${srcdir}/pam_mount.8.in >pam_mount.8;

pam_mount.conf.5: pam_mount.conf.5.in ${top_builddir}/config.status
	${AM_V_GEN}${replcmd} ${srcdir}/pam_mount.conf.5.in >pam_mount.conf.5;
