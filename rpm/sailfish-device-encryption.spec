Name:       sailfish-device-encryption
Summary:    Sailfish Device Encryption
Version:    0.2.11
Release:    1
License:    Proprietary
URL:        https://bitbucket.org/jolla/ui-sailfish-device-encryption
Source0:    %{name}-%{version}.tar.bz2

%define unitdir /usr/lib/systemd/system/
%define unit_conf_dir /etc/systemd/system/
%define dbusname org.sailfishos.EncryptionService
%define dbus_system_dir /usr/share/dbus-1/system.d
%define dbus_service_dir /usr/share/dbus-1/system-services

BuildRequires: qt5-qmake
BuildRequires: qt5-qttools
BuildRequires: qt5-qttools-linguist
BuildRequires: sailfish-minui-devel >= 0.0.23
BuildRequires: sailfish-minui-dbus-devel
BuildRequires: sailfish-minui-label-tool
BuildRequires: pkgconfig(dsme)
BuildRequires: pkgconfig(dsme_dbus_if)
BuildRequires: pkgconfig(glib-2.0)
BuildRequires: pkgconfig(libcryptsetup)
BuildRequires: pkgconfig(libdbusaccess)
BuildRequires: pkgconfig(libsystemd-daemon)
BuildRequires: pkgconfig(libresource)
BuildRequires: pkgconfig(mce)
BuildRequires: pkgconfig(ohm-ext-route)
BuildRequires: pkgconfig(openssl)
BuildRequires: pkgconfig(udisks2)
Requires:      %{name}-unlock-ui
Requires:      %{name}-service

%description
%{summary}.

%package unlock-ui
Summary:  Sailfish Encryption Unlock UI
Requires: sailfish-minui-resources
Requires: sailfish-content-graphics-default-base

%description unlock-ui
Password agent to unlock encrypted partitions on device boot.

%package service
Summary:  Sailfish Encryption Service
# Packages of commands required by home-encryption-*.sh scripts
Requires: coreutils
Requires: grep
Requires: shadow-utils
Requires: systemd
Requires: util-linux
Requires: oneshot

%description service
Encrypts home partition on request.

%package devel
Summary:  Development files for Sailfish Device Encryption
BuildRequires:  pkgconfig(Qt5Core)

%description devel
%{summary}.

%prep
%setup -q

%build
pushd sailfish-unlock-ui
%qmake5 "VERSION=%{version}"
make %{?_smp_mflags}
popd

pushd encryption-service
make %{?_smp_mflags}
popd

pushd libsailfishdeviceencryption
%qmake5 "VERSION=%{version}"
make %{?_smp_mflags}
popd

%install
rm -rf %{buildroot}

pushd sailfish-unlock-ui
%qmake5_install
popd

pushd encryption-service
make DESTDIR=%{buildroot} install
mkdir -p %{buildroot}/%{unitdir}/local-fs.target.wants/
ln -s ../home-encryption-preparation.service \
      %{buildroot}/%{unitdir}/local-fs.target.wants/

# Compatiblity and documentation files
mkdir -p %{buildroot}/%{_sysconfdir}
touch %{buildroot}/%{_sysconfdir}/crypttab
chmod 600 %{buildroot}/%{_sysconfdir}/crypttab
popd

pushd libsailfishdeviceencryption
%qmake5_install
popd

mkdir -p %{buildroot}%{_sharedstatedir}/%{name}

%files

%files unlock-ui
%defattr(-,root,root,-)
%{unitdir}/sailfish-unlock-agent.path
%{unitdir}/sailfish-unlock-agent.service
%{unitdir}/jolla-actdead-charging.service
%{unitdir}/sysinit.target.wants/sailfish-unlock-agent.path
%{unitdir}/systemd-cryptsetup@.service.d
%{_libexecdir}/sailfish-unlock-ui

%files service
%defattr(-,root,root,-)
%ghost %{_sysconfdir}/crypttab
%{_libexecdir}/sailfish-encryption-service
%{unitdir}/dbus-%{dbusname}.service
%{dbus_system_dir}/%{dbusname}.conf
%{dbus_service_dir}/%{dbusname}.service
%{unitdir}/home-encryption-preparation.service
%{unitdir}/local-fs.target.wants/home-encryption-preparation.service
%{unitdir}/mount-sd@.service.d/50-after-preparation.conf
%{unitdir}/home-mount-settle.service
%{_datadir}/%{name}
%dir %{_sharedstatedir}/%{name}
%ghost %dir %{unit_conf_dir}/multi-user.target.d
%ghost %config(noreplace) %{unit_conf_dir}/multi-user.target.d/50-home.conf
%ghost %dir %{unit_conf_dir}/systemd-user-sessions.service.d
%ghost %config(noreplace) %{unit_conf_dir}/systemd-user-sessions.service.d/50-home.conf
%ghost %dir %{unit_conf_dir}/home.mount.d
%ghost %config(noreplace) %{unit_conf_dir}/home.mount.d/50-settle.conf
%ghost %dir %{unit_conf_dir}/systemd-user-sessions.service.d
%ghost %config(noreplace) %{unit_conf_dir}/home-mount-settle.service.d/50-sailfish-home.conf
%ghost %config(noreplace) %{unit_conf_dir}/actdead.target.wants/jolla-actdead-charging.service

%files devel
%defattr(-,root,root,-)
%{_libdir}/libsailfishdeviceencryption.a
%{_libdir}/pkgconfig/sailfishdeviceencryption.pc
%{_includedir}/libsailfishdeviceencryption

%package ts-devel
Summary:  Translation source for Sailfish Encryption Unlock UI

%description ts-devel
%{summary}.

%files ts-devel
%defattr(-,root,root,-)
%{_datadir}/translations/source/sailfish-unlock-ui.ts

%package unlock-ui-resources-z1.0
Summary:    Scale factor 1.0 resources for the Sailfish Encryption Unlock UI
Provides:   %{name}-resources
Requires:   sailfish-minui-resources-z1.0

%description unlock-ui-resources-z1.0
%{summary}.

%files unlock-ui-resources-z1.0
%defattr(-,root,root,-)
%{_datadir}/sailfish-minui/images/z1.0/*.png

%package unlock-ui-resources-z1.25
Summary:    Scale factor 1.25 resources for the Sailfish Encryption Unlock UI
Provides:   %{name}-resources
Requires:   sailfish-minui-resources-z1.25

%description unlock-ui-resources-z1.25
%{summary}.

%files unlock-ui-resources-z1.25
%defattr(-,root,root,-)
%{_datadir}/sailfish-minui/images/z1.25/*.png

%package unlock-ui-resources-z1.5
Summary:    Scale factor 1.5 resources for the Sailfish Encryption Unlock UI
Provides:   %{name}-resources
Requires:   sailfish-minui-resources-z1.5

%description unlock-ui-resources-z1.5
%{summary}.

%files unlock-ui-resources-z1.5
%defattr(-,root,root,-)
%{_datadir}/sailfish-minui/images/z1.5/*.png

%package unlock-ui-resources-z1.5-large
Summary:    Scale factor 1.5 resources for the Sailfish Encryption Unlock UI
Provides:   %{name}-resources
Requires:   sailfish-minui-resources-z1.5-large

%description unlock-ui-resources-z1.5-large
%{summary}.

%files unlock-ui-resources-z1.5-large
%defattr(-,root,root,-)
%{_datadir}/sailfish-minui/images/z1.5-large/*.png

%package unlock-ui-resources-z1.75
Summary:    Scale factor 1.75 resources for the Sailfish Encryption Unlock UI
Provides:   %{name}-resources
Requires:   sailfish-minui-resources-z1.75

%description unlock-ui-resources-z1.75
%{summary}.

%files unlock-ui-resources-z1.75
%defattr(-,root,root,-)
%{_datadir}/sailfish-minui/images/z1.75/*.png

%package unlock-ui-resources-z2.0
Summary:    Scale factor 2.0 resources for the Sailfish Encryption Unlock UI
Provides:   %{name}-resources
Requires:   sailfish-minui-resources-z2.0

%description unlock-ui-resources-z2.0
%{summary}.

%files unlock-ui-resources-z2.0
%defattr(-,root,root,-)
%{_datadir}/sailfish-minui/images/z2.0/*.png
