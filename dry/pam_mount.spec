
Name:           pam_mount
Version:        0.15.0
Release:        0
Group:          System/Libraries
Summary:        A PAM module that can mount volumes for a user session
License:        LGPL
URL:            http://pam-mount.sf.net/

Source:         http://heanet.dl.sf.net/sourceforge/pam-mount/%name-%version.tar.bz2
Requires:       pam
BuildRequires:  autoconf automake binutils gcc glib2-devel libtool pam-devel openssl-devel zlib-devel
BuildRoot:      %_tmppath/%name-%version-build

%description
pam_mount automatically mounts directories when the user logs in,
using the password just entered.

pam_mount supports SMB, NCP, and any type of filesystem that can 
be mounted using the standard mount command.

## Remove the debug_package line to compile under FedoraCore
%debug_package
%prep
%setup

%build
autoreconf -fi;
%configure --disable-debug --libdir=/lib
make;

%install
b="%buildroot";
rm -Rf "$b";
make -i install DESTDIR="$b";
mkdir -p "$b/%_sysconfdir/security";
install -m0644 config/pam_mount.conf "$b/%_sysconfdir/security/";
rm -f "$b/%_lib/security/"*.{la,a};

%clean
rm -Rf "%buildroot";

%files
%defattr(-,root,root)
%config(noreplace) %_sysconfdir/security/%name.conf
/%_lib/security/%{name}*.so
%_sbindir/pmvarrun
%_bindir/mkehd
%_bindir/autoehd
%_bindir/passwdehd
%_bindir/mount_ehd
/sbin/mount.crypt
/sbin/umount.crypt
%_mandir/man8/*
#%policy %_sysconfdir/selinux/strict/src/policy/macros/%{name}_macros.te
#%policy %_sysconfdir/selinux/strict/src/policy/file_contexts/misc/%name.fc
%doc AUTHORS COPYING ChangeLog INSTALL NEWS README FAQ TODO

%changelog -n pam_mount
