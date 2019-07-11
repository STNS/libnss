Summary:          SimpleTomlNameService Nss Module
Name:             libnss-stns-v2
Version:          2.3.3
Release:          2
License:          GPLv3
URL:              https://github.com/STNS/STNS
Source:           %{name}-%{version}.tar.gz
Group:            System Environment/Base
Packager:         pyama86 <www.kazu.com@gmail.com>
%if 0%{?rhel} < 7
Requires:         glibc openssl
BuildRequires:    gcc make openssl-devel
%else
Requires:         glibc openssl-libs
BuildRequires:    gcc make openssl-devel
%endif
BuildRoot:        %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)
BuildArch:        i386, x86_64

%define debug_package %{nil}

%description
We provide name resolution of Linux user group using STNS.

%prep
%setup -q -n %{name}-%{version}

%build
make

%install
%{__rm} -rf %{buildroot}
mkdir -p %{buildroot}/usr/{lib64,bin}
mkdir -p %{buildroot}%{_sysconfdir}
make PREFIX=%{buildroot}/usr install
install -d -m 0777 %{buildroot}/var/cache/stns
install -d -m 0744 %{buildroot}%{_sysconfdir}/stns/client/
install -m 644 stns.conf.example %{buildroot}%{_sysconfdir}/stns/client/stns.conf

%clean
%{__rm} -rf %{buildroot}

%post
sed -i "s/^IPAddressDeny=any/#IPAddressDeny=any/" /lib/systemd/system/systemd-logind.service || true
systemctl status systemd-logind && systemctl daemon-reload && systemctl restart systemd-logind

%preun

%postun

%files
%defattr(-, root, root)
%doc stns.conf.example
/usr/lib64/libnss_stns.so
/usr/lib64/libnss_stns.so.2
/usr/lib64/libnss_stns.so.2.0
/usr/lib/stns/stns-key-wrapper
/usr/local/bin/stns-key-wrapper
/var/cache/stns
%config(noreplace) /etc/stns/client/stns.conf

%changelog
* Thu Jul 11 2019 pyama86 <www.kazu.com@gmail.com> - 2.3.3-2
- #22 Add --no-pager(@k1low)
* Fri Jul 5 2019 pyama86 <www.kazu.com@gmail.com> - 2.3.3-1
- #21 Need daemon-reload after rewriting service file
* Wed Jul 3 2019 pyama86 <www.kazu.com@gmail.com> - 2.3.2-1
- #20 restart systemd-logind when install
* Sun Mar 10 2019 pyama86 <www.kazu.com@gmail.com> - 2.3.1-1
- #14 unsupport porotcol is return code zero
* Sat Mar 9 2019 pyama86 <www.kazu.com@gmail.com> - 2.3.0-1
- #10 Free configuration on error
- #13 FIX #11 check return code load config
* Fri Mar 1 2019 pyama86 <www.kazu.com@gmail.com> - 2.2.3-1
- #7 allow uppercase alphabet in validation(@miya-sun)
- #8 Call header_handler even if server returned 4xx response(@ry023)
* Wed Feb 27 2019 pyama86 <www.kazu.com@gmail.com> - 2.2.2-1
- #6 support http header
* Tue Feb 26 2019 pyama86 <www.kazu.com@gmail.com> - 2.2.1-1
- #5 url validation was wrong(thanks: @miya-sun)
* Thu Feb 21 2019 pyama86 <www.kazu.com@gmail.com> - 2.2.0-1
- #3 Support TLS Authentication
* Thu Jan 24 2019 pyama86 <www.kazu.com@gmail.com> - 2.1.0-1
- #1 There are cases where mutex deadlocks
* Sat Nov 10 2018 pyama86 <www.kazu.com@gmail.com> - 2.0.3-1
- #80 Ignore sigpipe signal in key wrapper
* Fri Nov 2 2018 pyama86 <www.kazu.com@gmail.com> - 2.0.2-1
- #79 add sticky bit to cache dir
* Thu Nov 1 2018 pyama86 <www.kazu.com@gmail.com> - 2.0.1-1
- #78 Cache can be vulnerable
* Wed Oct 3 2018 pyama86 <www.kazu.com@gmail.com> - 2.0.0-1
- #76 FIX SEGV if too many members
* Thu Sep 20 2018 pyama86 <www.kazu.com@gmail.com> - 1.0.0-6
- #73 fix id shift did not go well
* Tue Sep 11 2018 pyama86 <www.kazu.com@gmail.com> - 1.0.0-5
- #71 fix segv when sudo
* Tue Sep 11 2018 pyama86 <www.kazu.com@gmail.com> - 1.0.0-4
- #68 json_value_free may be segv
* Mon Sep 10 2018 pyama86 <www.kazu.com@gmail.com> - 1.0.0-3
- #65 I made http proxy available at http request
- #66 Replaced json library
* Tue Sep 4 2018 pyama86 <www.kazu.com@gmail.com> - 1.0.0-2
- Add symbolic link to key-wrapper
* Mon Sep 3 2018 pyama86 <www.kazu.com@gmail.com> - 1.0.0-1
- Release
* Mon Aug 27 2018 pyama86 <www.kazu.com@gmail.com> - 0.0.1-1
- Initial packaging
