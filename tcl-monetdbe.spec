%{!?directory:%define directory /usr}

%define buildroot %{_tmppath}/%{name}
%define packagename monetdbe

Name:          tcl-monetdbe
Summary:       Tcl extension for MonetDB Embedded
Version:       0.3.0
Release:       1
License:       MPL-2.0
Group:         Development/Libraries/Tcl
Source:        tcl-monetdbe-%{version}.tar.gz
URL:           https://github.com/ray2501/tcl-monetdbe
BuildRequires: autoconf
BuildRequires: make
BuildRequires: tcl-devel >= 8.6
Requires:      tcl >= 8.6
BuildRoot:     %{buildroot}

%description
tcl-monetdbe is a Tcl extension for MonetDB Embedded library.

This extension is using Tcl_LoadFile to load MonetDB/e library.

%prep
%setup -q -n %{name}-%{version}

%build
./configure \
	--prefix=%{directory} \
	--exec-prefix=%{directory} \
	--libdir=%{directory}/%{_lib}
make 

%install
make DESTDIR=%{buildroot} pkglibdir=%{tcl_archdir}/%{packagename}%{version} install

%clean
rm -rf %buildroot

%files
%defattr(-,root,root)
%{tcl_archdir}
