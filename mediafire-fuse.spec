Name:           mediafire-fuse
Version:        0.6
Release:        1%{?dist}
Summary:        fuse module that is able to mount the mediafire share locally.
License:        GPLv2
URL:            http:www.mediafire.com 
Source0:        %{name}-%{version}.tar.gz 
BuildRequires:  cmake, jansson-devel, libcurl, libcurl-devel, fuse-devel, openssl-devel
#Requires:       

%description
The mediafire-tools project offers these programs
to interact with a mediafire account:
mediafire-shell: a simple shell for a mediafire 
account like ftp(1).
mediafire-fuse: a fuse module that is able to mount
the mediafire share locally.
Coming soon:
mediafire-check: a tool that will check to see if a file
is eligible for instant upload or whether or not a file


%prep
%setup -q
mkdir -p build; cd build; cmake ..; pwd;


%build
cd build; make


%install
rm -rf $RPM_BUILD_ROOT
cd build; make install DESTDIR=$RPM_BUILD_ROOT


%clean
rm -rf $RPM_BUILD_ROOT
rm -rf $RPM_BUILD_ROOT/build


%files
%defattr(-,root,root,-)
/usr/local/bin/mediafire-*


%changelog
* Wed Oct 07 2015 Adrian Alves <aalves@gmail.com> - 0.6-1
- Initial build
