Name:       sailfish-device-encryption
Summary:    Sailfish Device Encryption
Version:    0.7.3
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
BuildRequires: sailfish-minui-devel >= 0.0.24
BuildRequires: sailfish-minui-dbus-devel
BuildRequires: sailfish-minui-label-tool
BuildRequires: sailfish-svg2png >= 0.3.4
BuildRequires: systemd
BuildRequires: %{name}-l10n-all-translations
BuildRequires: usb-moded-devel
BuildRequires: pkgconfig(sailfishaccesscontrol) >= 0.0.3
BuildRequires: pkgconfig(dsme)
BuildRequires: pkgconfig(dsme_dbus_if)
BuildRequires: pkgconfig(glib-2.0)
BuildRequires: pkgconfig(libcryptsetup)
BuildRequires: pkgconfig(libdbusaccess)
BuildRequires: pkgconfig(libresource)
BuildRequires: pkgconfig(mce)
BuildRequires: pkgconfig(ohm-ext-route)
BuildRequires: pkgconfig(openssl)
BuildRequires: pkgconfig(udisks2)
BuildRequires: pkgconfig(Qt5Qml)
BuildRequires: pkgconfig(Qt5Quick)
BuildRequires: pkgconfig(Qt5Gui)
Requires:      %{name}-unlock-ui
Requires:      %{name}-service
Requires:      %{name}-settings
Requires:      sailfish-setup >= 0.1.2

%description
%{summary}.

%package unlock-ui
Summary:  Sailfish Encryption Unlock UI
Requires: sailfish-minui-resources

%description unlock-ui
Password agent to unlock encrypted partitions on device boot.

%package service
Summary:  Sailfish Encryption Service
# Packages of commands required by home-encryption-*.sh scripts
Requires: coreutils
Requires: grep
Requires: shadow-utils >= 4.8.1
Requires: systemd
Requires: util-linux
Requires: oneshot

%description service
Encrypts home partition on request.

%package settings
Summary:  Settings plugin for encryption
BuildRequires: pkgconfig(systemsettings) >= 0.5.26
Requires: jolla-settings
Requires: jolla-settings-system
Requires: %{name}-service = %{version}-%{release}
Requires: %{name}-unlock-ui = %{version}-%{release}

%description settings
%{summary}.

%package devel
Summary:  Development files for Sailfish Device Encryption
BuildRequires:  pkgconfig(Qt5Core)

%description devel
%{summary}.

%package qa
Summary:  Encryption tool for QA
Requires: sed
Requires: oneshot
%{_oneshot_requires_post}
# This can not be required here because otherwise
# sailfish-device-encryption would be pulled to every QA image
# Requires: %%{name} = %%{version}-%%{release}

%description qa
%{summary}.

%post qa
if [ "$1" -eq 1 ]; then
    %{_bindir}/add-oneshot --late 50-enable-home-encryption
fi

%prep
%setup -q

%build
pushd libsailfishdeviceencryption
%qmake5 "VERSION=%{version}"
make %{?_smp_mflags}
popd

pushd sailfish-unlock-ui
%qmake5 "VERSION=%{version}"
make %{?_smp_mflags}
popd

pushd jolla-settings-encryption
%qmake5 "VERSION=%{version}"
make %{?_smp_mflags}
popd

pushd encryption-service
make %{?_smp_mflags}
popd

pushd qa-encrypt-device
%qmake5 "VERSION=%{version}"
make %{?_smp_mflags}
popd

%install
rm -rf %{buildroot}

pushd sailfish-unlock-ui
%qmake5_install
popd

pushd jolla-settings-encryption
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

pushd qa-encrypt-device
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
%{unitdir}/packagekit.service.d/01-home-mount.conf
%{unitdir}/aliendalvik.service.d/01-prevent-start.conf
%{unitdir}/connman.service.d/01-prevent-start.conf
%{unitdir}/connman-vpn.service.d/01-prevent-start.conf
%{unitdir}/dbus-org.nemomobile.MmsEngine.service.d/01-prevent-start.conf
%{unitdir}/dbus-org.nemomobile.provisioning.service.d/01-prevent-start.conf
%{unitdir}/mdm_proxy.service.d/01-prevent-start.conf
%{unitdir}/packagekit.service.d/01-prevent-start.conf
%{unitdir}/home-mount-settle.service
%{_datadir}/%{name}
%dir %{_sharedstatedir}/%{name}
%ghost %dir %{unit_conf_dir}/multi-user.target.d
%ghost %config(noreplace) %{unit_conf_dir}/multi-user.target.d/50-home.conf
%ghost %dir %{unit_conf_dir}/systemd-user-sessions.service.d
%ghost %config(noreplace) %{unit_conf_dir}/systemd-user-sessions.service.d/50-home.conf
%ghost %dir %{unit_conf_dir}/home.mount.d
%ghost %config(noreplace) %{unit_conf_dir}/home.mount.d/50-settle.conf
%ghost %config(noreplace) %{unit_conf_dir}/home-mount-settle.service.d/50-sailfish-home.conf
%ghost %config(noreplace) %{unit_conf_dir}/actdead.target.wants/jolla-actdead-charging.service

%pre service
# Add marker file if we are on Imager and remove otherwise
if [ -f "/.bootstrap" ]; then
  mkdir -p %{_sharedstatedir}/sailfish-device-encryption || :
  touch %{_sharedstatedir}/sailfish-device-encryption/encrypt-home || :
else
  rm -f %{_sharedstatedir}/sailfish-device-encryption/encrypt-home || :
fi

%files settings
%defattr(-,root,root,-)
%{_datadir}/jolla-settings
%{_libdir}/qt5/qml/Sailfish/Encryption
%{_datadir}/translations/settings-*.qm

%files devel
%defattr(-,root,root,-)
%{_libdir}/libsailfishdeviceencryption.a
%{_libdir}/pkgconfig/sailfishdeviceencryption.pc
%{_includedir}/libsailfishdeviceencryption

%files qa
%defattr(-,root,root,-)
/usr/lib/startup/qa-encrypt-device
%{_datadir}/qa-encrypt-device/main.qml
%attr(755, root, -) %{_oneshotdir}/50-enable-home-encryption
%{unitdir}/vnc.service.d/01-prevent-start.conf
%{_userunitdir}/ambienced.service.d/01-prevent-start.conf

%package ts-devel
Summary:  Translation source for Sailfish Encryption Unlock UI

%description ts-devel
%{summary}.

%files ts-devel
%defattr(-,root,root,-)
%{_datadir}/translations/source/*.ts

%define sailfish_content_graphics_for_scale(z:d:) \
%package unlock-ui-resources-%{-z*} \
Summary:    Scale factor %{-d*} resources for the Sailfish Encryption Unlock UI \
Provides:   %{name}-resources \
Requires:   sailfish-minui-resources-%{-z*} \
\
%description unlock-ui-resources-%{-z*} \
%{summary}. \
\
%files unlock-ui-resources-%{-z*} \
%defattr(-,root,root,-) \
%{_datadir}/sailfish-minui/images/%{-z*}/*.png

%sailfish_content_graphics_for_each_scale

