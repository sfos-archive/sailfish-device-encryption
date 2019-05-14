Name:       sailfish-device-encryption
Summary:    Sailfish Device Encryption
Version:    0.1.0
Release:    1
License:    Proprietary
URL:        https://bitbucket.org/jolla/ui-sailfish-device-encryption
Source0:    %{name}-%{version}.tar.bz2

%define unitdir /lib/systemd/system/
%define dbusname org.sailfishos.EncryptionService
%define dbus_system_dir /usr/share/dbus-1/system.d
%define dbus_service_dir /usr/share/dbus-1/system-services

BuildRequires: qt5-qmake
BuildRequires: qt5-qttools
BuildRequires: qt5-qttools-linguist
BuildRequires: sailfish-minui-devel >= 0.0.6
BuildRequires: sailfish-minui-label-tool
BuildRequires: pkgconfig(glib-2.0)
BuildRequires: pkgconfig(libdbusaccess)
BuildRequires: pkgconfig(libsystemd-daemon)
BuildRequires: pkgconfig(libudev)
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
Requires: grep
Requires: shadow-utils
Requires: systemd
Requires: util-linux
Requires: oneshot

%description service
Encrypts home partition on request.

%prep
%setup -q

%build
pushd unlock-agent
%qmake5 "VERSION=%{version}"
make %{?_smp_mflags}
popd

pushd encryption-service
make %{?_smp_mflags}
popd

%install
rm -rf %{buildroot}

pushd unlock-agent
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

mkdir -p %{buildroot}%{_sharedstatedir}/%{name}

%files

%files unlock-ui
%defattr(-,root,root,-)
%{unitdir}/sailfish-unlock-agent.path
%{unitdir}/sailfish-unlock-agent.service
%{unitdir}/sysinit.target.wants/sailfish-unlock-agent.path
%{_libexecdir}/sailfish-unlock-ui
%{_prefix}/share/sailfish-minui/images

%files service
%defattr(-,root,root,-)
%ghost %{_sysconfdir}/crypttab
%{_libexecdir}/sailfish-encryption-service
%{unitdir}/dbus-%{dbusname}.service
%{dbus_system_dir}/%{dbusname}.conf
%{dbus_service_dir}/%{dbusname}.service
%{unitdir}/home-encryption-preparation.service
%{unitdir}/local-fs.target.wants/home-encryption-preparation.service
%{_datadir}/%{name}
%dir %{_sharedstatedir}/%{name}

%package unlock-ui-ts-devel
Summary:  Translation source for Sailfish Encryption Unlock UI

%description unlock-ui-ts-devel
%{summary}.

%files unlock-ui-ts-devel
%defattr(-,root,root,-)
%{_datadir}/translations/source/sailfish-unlock-ui.ts
