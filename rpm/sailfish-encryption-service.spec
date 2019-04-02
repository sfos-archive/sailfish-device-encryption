Name:       sailfish-encryption-service
Summary:    Sailfish Encryption Service
Version:    0.1.0
Release:    1
Group:      System/Base
License:    Proprietary
URL:        https://bitbucket.org/jolla/ui-sailfish-device-encryption
Source0:    %{name}-%{version}.tar.bz2
BuildRequires: libudisks2-devel

%define dbusname org.sailfishos.EncryptionService
%define unitdir /lib/systemd/system
%define dbus_system_dir /usr/share/dbus-1/system.d
%define dbus_service_dir /usr/share/dbus-1/system-services

%description
Encrypts home partition on request.

%prep
%setup -q -n %{name}-%{version}/encryption-service

%build
make %{_smp_mflags}

%install
rm -rf %{buildroot}
make DESTDIR=%{buildroot} install
mkdir %{buildroot}/%{unitdir}/sysinit.target.wants/
ln -s ../dbus-%{dbusname}.service %{buildroot}/%{unitdir}/sysinit.target.wants/

%files
%defattr(-,root,root,-)
%{_sbindir}/sailfish-encryption-service
%{unitdir}/dbus-%{dbusname}.service
%{unitdir}/sysinit.target.wants/dbus-%{dbusname}.service
%{dbus_system_dir}/%{dbusname}.conf
%{dbus_service_dir}/%{dbusname}.service
