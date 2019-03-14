Name:       sailfish-unlock-ui
Summary:    Sailfish Unlock
Version:    0.1.0
Release:    1
Group:      System/Base
License:    Proprietary
URL:        https://bitbucket.org/jolla/ui-sailfish-unlock-ui
Source0:    %{name}-%{version}.tar.bz2

%define unitdir /lib/systemd/system/

BuildRequires: qt5-qmake
BuildRequires: qt5-qttools
BuildRequires: qt5-qttools-linguist
BuildRequires: sailfish-minui-devel
BuildRequires: sailfish-minui-label-tool
BuildRequires: pkgconfig(libsystemd-daemon)
Requires: sailfish-minui-resources
Requires: sailfish-content-graphics-default-base

%description
Password agent to unlock encrypted partitions on device boot.

%prep
%setup -q -n %{name}-%{version}/unlock-agent

%build
%qmake5 "VERSION=%{version}"
make %{?_smp_mflags}

%install
rm -rf %{buildroot}
%qmake5_install

%files
%defattr(-,root,root,-)
%{unitdir}/sailfish-unlock-agent.path
%{unitdir}/sailfish-unlock-agent.service
%{unitdir}/sysinit.target.wants/sailfish-unlock-agent.path
%{_sbindir}/%{name}
%exclude %{_datadir}/translations/source/%{name}.ts
%{_prefix}/share/sailfish-minui/images
