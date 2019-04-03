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
BuildRequires: sailfish-minui-devel
BuildRequires: sailfish-minui-label-tool
BuildRequires: pkgconfig(libsystemd-daemon)
BuildRequires: pkgconfig(udisks2)
Requires:      %{name}-agent
Requires:      %{name}-service

%description
%{summary}.

%package agent
Summary:  Sailfish Unlock
Requires: sailfish-minui-resources
Requires: sailfish-content-graphics-default-base

%description agent
Password agent to unlock encrypted partitions on device boot.

%package service
Summary:  Sailfish Encryption Service

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
mkdir -p %{buildroot}/%{unitdir}/sysinit.target.wants/
ln -s ../dbus-%{dbusname}.service %{buildroot}/%{unitdir}/sysinit.target.wants/
mkdir -p %{buildroot}/%{unitdir}/local-fs.target.wants/
ln -s ../home-encryption-preparation.service \
      %{buildroot}/%{unitdir}/local-fs.target.wants/
popd

%files

%files agent
%defattr(-,root,root,-)
%{unitdir}/sailfish-unlock-agent.path
%{unitdir}/sailfish-unlock-agent.service
%{unitdir}/sysinit.target.wants/sailfish-unlock-agent.path
%{_sbindir}/sailfish-unlock-ui
%exclude %{_datadir}/translations/source/sailfish-unlock-ui.ts
%{_prefix}/share/sailfish-minui/images

%files service
%defattr(-,root,root,-)
%{_sbindir}/sailfish-encryption-service
%{unitdir}/dbus-%{dbusname}.service
%{unitdir}/sysinit.target.wants/dbus-%{dbusname}.service
%{dbus_system_dir}/%{dbusname}.conf
%{dbus_service_dir}/%{dbusname}.service
%{unitdir}/home-encryption-preparation.service
%{unitdir}/local-fs.target.wants/home-encryption-preparation.service
%{_datadir}/%{name}
