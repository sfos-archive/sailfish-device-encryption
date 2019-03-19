Name:       sailfish-unlock-ui
Summary:    Sailfish Unlock
Version:    0.1.0
Release:    1
Group:      System/Base
License:    Proprietary
Url:        https://bitbucket.org/jolla/ui-sailfish-unlock-ui
Source0:    %{name}-%{version}.tar.bz2

%define unitdir /lib/systemd/system/

%description
Password agent to unlock encrypted partitions on device boot.

%prep
%setup -q -n %{name}-%{version}/unlock-agent

%build
make %{_smp_mflags}

%install
rm -rf %{buildroot}
make DESTDIR=%{buildroot} install

%files
%defattr(-,root,root,-)
%{_bindir}/unlock-agent
%{unitdir}/sailfish-unlock-agent.path
%{unitdir}/sailfish-unlock-agent.service
%{unitdir}/sysinit.target.wants/sailfish-unlock-agent.path
