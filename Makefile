# The base of this code is https://github.com/pyama86/stns/blob/master/Makefile
CC=gcc
CFLAGS=-Os -Wall -Wstrict-prototypes -Werror -fPIC -std=c99 -D_GNU_SOURCE -I$(CURL_DIR)/include -I$(OPENSSL_DIR)/include
STNS_LDFLAGS=-Wl,--version-script,libstns.map

LIBRARY=libnss_stns.so.2.0
KEY_WRAPPER=stns-key-wrapper
LINKS=libnss_stns.so.2 libnss_stns.so
LD_SONAME=-Wl,-soname,libnss_stns.so.2
VERSION = $(shell cat version)

PREFIX=/usr
LIBDIR ?= $(PREFIX)/lib64
BINDIR=$(PREFIX)/lib/stns
BINSYMDIR=$(PREFIX)/local/bin/


CRITERION_VERSION=2.4.2
SHUNIT_VERSION=2.1.8
CURL_VERSION_TAG=8_8_0
CURL_VERSION=$(shell echo $(CURL_VERSION_TAG) | sed -e 's/_/./g')
OPENSSL_VERSION=3.3.1
ZLIB_VERSION=1.3.1

DIST ?= unknown
STNSD_VERSION=0.3.13

DIST_DIR:=$(shell pwd)/tmp/$(DIST)
SRC_DIR:=$(DIST_DIR)/src
STNS_DIR:=$(DIST_DIR)/stns
OPENSSL_DIR:=$(DIST_DIR)/openssl-$(OPENSSL_VERSION)
CURL_DIR:=$(DIST_DIR)/curl-$(CURL_VERSION)
ZLIB_DIR:=$(DIST_DIR)/zlib-$(ZLIB_VERSION)
SOURCES=Makefile stns.h stns.c stns*.c stns*.h toml.h toml.c parson.h parson.c stns.conf.example test libstns.map

STATIC_LIBS=$(CURL_DIR)/lib/libcurl.a \
	    $(OPENSSL_DIR)/lib/libssl.a  \
	    $(OPENSSL_DIR)/lib/libcrypto.a \
	    $(ZLIB_DIR)/lib/libz.a

LIBS_CFLAGS=-Os -fPIC
CURL_LDFLAGS := -L$(OPENSSL_DIR)/lib

MAKE=make -j4
default: build
ci: curl test integration
test: cleanup testdev ## Test with dependencies installation
	@echo "$(INFO_COLOR)==> $(RESET)$(BOLD)Testing$(RESET)"
	mkdir -p /etc/stns/client/
	echo 'api_endpoint = "http://httpbin"' > /etc/stns/client/stns.conf
	sudo service cache-stnsd restart
	ASAN_OPTIONS=detect_leaks=1:exitcode=1:abort_on_error=true $(CC) -g3 -fsanitize=address -O0 -fno-omit-frame-pointer -I$(CURL_DIR)/include \
	  stns.c stns_group.c toml.c parson.c stns_shadow.c stns_passwd.c stns_test.c stns_group_test.c stns_shadow_test.c stns_passwd_test.c \
		$(STATIC_LIBS) \
		-lcriterion \
		-lpthread \
		-ldl \
		-lrt \
		-o $(DIST_DIR)/test
		$(DIST_DIR)/test --verbose

clean:
	rm -rf $(DIST_DIR)
	rm -rf $(SRC_DIR)
	rm -rf $(STNS_DIR)

build_dir: ## Create directory for build
	test -d $(DIST_DIR) || mkdir -p $(DIST_DIR)
	test -d $(SRC_DIR) || mkdir -p $(SRC_DIR)
	test -d $(STNS_DIR) || mkdir -p $(STNS_DIR)

zlib:  build_dir
	test -d $(SRC_DIR)/zlib-$(ZLIB_VERSION) || (curl -sL https://zlib.net/zlib-$(ZLIB_VERSION).tar.xz -o $(SRC_DIR)/zlib-$(ZLIB_VERSION).tar.xz && cd $(SRC_DIR) && tar xf zlib-$(ZLIB_VERSION).tar.xz)
	test -f $(ZLIB_DIR)/lib/libz.a || (cd $(SRC_DIR)/zlib-$(ZLIB_VERSION) && (make clean |true) && CFLAGS='$(LIBS_CFLAGS)' ./configure \
	  --prefix=$(ZLIB_DIR) \
	&& $(MAKE) && $(MAKE) install)

openssl: build_dir zlib
	test -d $(SRC_DIR)/openssl-$(OPENSSL_VERSION) || (curl -sL https://www.openssl.org/source/openssl-$(OPENSSL_VERSION).tar.gz -o $(SRC_DIR)/openssl-$(OPENSSL_VERSION).tar.gz && cd $(SRC_DIR) && tar xf openssl-$(OPENSSL_VERSION).tar.gz)
	test -f $(OPENSSL_DIR)/lib/libssl.a || (cd $(SRC_DIR)/openssl-$(OPENSSL_VERSION) && (make clean |true) && CFLAGS='$(LIBS_CFLAGS)' ./config \
	  --prefix=$(OPENSSL_DIR) \
	  --openssldir=$(OPENSSL_DIR) \
	  --libdir=lib \
	  no-shared \
	  no-ssl3 \
	  no-asm \
	  -Wl,--enable-new-dtags \
	  && $(MAKE) depend && $(MAKE) && $(MAKE) install)

curl: build_dir openssl
	test -d $(SRC_DIR)/curl-$(CURL_VERSION) || (curl -sL https://curl.haxx.se/download/curl-$(CURL_VERSION).tar.gz -o $(SRC_DIR)/curl-$(CURL_VERSION).tar.gz && cd $(SRC_DIR) && tar xf curl-$(CURL_VERSION).tar.gz)
	test -f $(CURL_DIR)/lib/libcurl.a || (cd $(SRC_DIR)/curl-$(CURL_VERSION) && (make clean | true) && \
	  LIBS="-ldl -lpthread" LDFLAGS="$(CURL_LDFLAGS)" CFLAGS='$(LIBS_CFLAGS)' ./configure \
	  --with-ssl=$(OPENSSL_DIR) \
	  --with-zlib=$(ZLIB_DIR) \
	  --enable-libcurl-option \
	  --without-libpsl \
	  --disable-shared \
	  --enable-static \
	  --prefix=$(CURL_DIR) \
	  --disable-ldap \
	  --disable-sspi \
	  --without-librtmp \
	  --disable-ftp \
	  --disable-file \
	  --disable-dict \
	  --disable-telnet \
	  --disable-tftp \
	  --disable-rtsp \
	  --disable-pop3 \
	  --disable-imap \
	  --disable-smtp \
	  --disable-gopher \
	  --disable-smb && \
	  $(MAKE) && $(MAKE) install)

criterion:  ## Installing dependencies for development
	mkdir -p $(DIST_DIR)
	test -f $(DIST_DIR)/criterion.tar.xz || curl -sL https://github.com/Snaipe/Criterion/releases/download/v$(CRITERION_VERSION)/criterion-$(CRITERION_VERSION)-linux-x86_64.tar.xz -o $(DIST_DIR)/criterion.tar.xz
	test -d /usr/include/criterion || (cd $(DIST_DIR) && tar xf criterion.tar.xz && cd ../)
	test -d /usr/include/criterion || (mv $(DIST_DIR)/criterion-$(CRITERION_VERSION)/include/criterion /usr/include/criterion && mv $(DIST_DIR)/criterion-$(CRITERION_VERSION)/lib/libcriterion.* /usr/lib/)
	test -f $(DIST_DIR)/shunit2.tgz || curl -sL https://github.com/kward/shunit2/archive/refs/tags/v$(SHUNIT_VERSION).tar.gz -o $(DIST_DIR)/shunit2.tgz
	cd $(DIST_DIR); tar xf shunit2.tgz; cd ../
	test -d /usr/include/shunit2 || mv $(DIST_DIR)/shunit2-$(SHUNIT_VERSION)/ /usr/include/shunit2

debug:
	@echo "$(INFO_COLOR)==> $(RESET)$(BOLD)Testing$(RESET)"
	$(CC) -g -I$(CURL_DIR)/include \
	  test/debug.c stns.c stns_group.c toml.c parson.c stns_shadow.c stns_passwd.c \
		$(STATIC_LIBS) \
		 -lpthread -ldl -o $(DIST_DIR)/debug && \
		$(DIST_DIR)/debug && valgrind --leak-check=full tmp/libs/debug

testdev: build_dir curl criterion ## Test without dependencies installation

build: nss_build key_wrapper_build
nss_build : build_dir curl ## Build nss_stns
	@echo "$(INFO_COLOR)==> $(RESET)$(BOLD)Building nss_stns$(RESET)"
	cd /stns
	$(CC) $(STNS_LDFLAGS) $(CFLAGS) -c parson.c -o $(STNS_DIR)/parson.o
	$(CC) $(STNS_LDFLAGS) $(CFLAGS) -c toml.c -o $(STNS_DIR)/toml.o
	$(CC) $(STNS_LDFLAGS) $(CFLAGS) -c stns_passwd.c -o $(STNS_DIR)/stns_passwd.o
	$(CC) $(STNS_LDFLAGS) $(CFLAGS) -c stns_group.c -o $(STNS_DIR)/stns_group.o
	$(CC) $(STNS_LDFLAGS) $(CFLAGS) -c stns_shadow.c -o $(STNS_DIR)/stns_shadow.o
	$(CC) $(STNS_LDFLAGS) $(CFLAGS) -c stns.c -o $(STNS_DIR)/stns.o
	 $(CC) $(STNS_LDFLAGS) -shared $(LD_SONAME) -o $(STNS_DIR)/$(LIBRARY) \
		$(STNS_DIR)/stns.o \
		$(STNS_DIR)/stns_passwd.o \
		$(STNS_DIR)/parson.o \
		$(STNS_DIR)/toml.o \
		$(STNS_DIR)/stns_group.o \
		$(STNS_DIR)/stns_shadow.o \
		$(STATIC_LIBS) \
		-lpthread \
		-ldl \
		-lrt

key_wrapper_build: build_dir ## Build key wrapper
	@echo "$(INFO_COLOR)==> $(RESET)$(BOLD)Building nss_stns$(RESET)"
	$(CC) $(CFLAGS) -c toml.c -o $(STNS_DIR)/toml.o
	$(CC) $(CFLAGS) -c parson.c -o $(STNS_DIR)/parson.o
	$(CC) $(CFLAGS) -c stns_key_wrapper.c -o $(STNS_DIR)/stns_key_wrapper.o
	$(CC) $(CFLAGS) -c stns.c -o $(STNS_DIR)/stns.o
	$(CC) -o $(STNS_DIR)/$(KEY_WRAPPER) \
		$(STNS_DIR)/stns.o \
		$(STNS_DIR)/stns_key_wrapper.o \
		$(STNS_DIR)/parson.o \
		$(STNS_DIR)/toml.o \
		$(STATIC_LIBS) \
		-lpthread \
		-ldl \
		-lrt

integration: cleanup testdev build install ## Run integration test
	@echo "$(INFO_COLOR)==> $(RESET)$(BOLD)Integration Testing$(RESET)"
	mkdir -p /etc/stns/client
	mkdir -p /etc/stns/server
	cp test/integration_client.conf /etc/stns/client/stns.conf && sudo service cache-stnsd restart
	cp test/integration_server.conf /etc/stns/server/stns.conf && sudo service stns restart
	bash -l -c "while ! nc -vz -w 1 127.0.0.1 1104 > /dev/null 2>&1; do sleep 1; echo 'sleeping'; done"
	test -d /usr/lib/x86_64-linux-gnu && ln -sf /usr/lib/libnss_stns.so.2.0 /usr/lib/x86_64-linux-gnu/libnss_stns.so.2.0 || true
	sed -i -e 's/^passwd:.*/passwd: files stns/g' /etc/nsswitch.conf
	sed -i -e 's/^shadow:.*/shadow: files stns/g' /etc/nsswitch.conf
	sed -i -e 's/^group:.*/group: files stns/g' /etc/nsswitch.conf
	grep test /etc/sudoers || echo 'test ALL=(ALL) NOPASSWD:ALL' >> /etc/sudoers
	test/integration_test.sh
	echo "use_cached = true" >> /etc/stns/client/stns.conf && sudo service cache-stnsd restart
	test/integration_test.sh

install: install_lib install_key_wrapper ## Install stns

install_lib: ## Install only shared objects
	@echo "$(INFO_COLOR)==> $(RESET)$(BOLD)Installing as Libraries$(RESET)"
	[ -d $(LIBDIR) ] || install -d $(LIBDIR)
	install $(STNS_DIR)/$(LIBRARY) $(LIBDIR)
	cd $(LIBDIR); for link in $(LINKS); do ln -sf $(LIBRARY) $$link ; done;

install_key_wrapper: ## Install only key wrapper
	@echo "$(INFO_COLOR)==> $(RESET)$(BOLD)Installing as Key Wrapper$(RESET)"
	[ -d $(BINDIR) ] || install -d $(BINDIR)
	[ -d $(BINSYMDIR) ] || install -d $(BINSYMDIR)
	install $(STNS_DIR)/$(KEY_WRAPPER) $(BINDIR)
	ln -sf /usr/lib/stns/$(KEY_WRAPPER) $(BINSYMDIR)/

source_for_rpm: ## Create source for RPM
	@echo "$(INFO_COLOR)==> $(RESET)$(BOLD)Distributing$(RESET)"
	rm -rf $(STNS_DIR) libnss-stns-v2-$(VERSION).tar.gz
	mkdir -p $(STNS_DIR)/libnss-stns-v2-$(VERSION)
	mkdir -p $(STNS_DIR)/libnss-stns-v2-$(VERSION)/tmp
	cp -r $(SOURCES) $(STNS_DIR)/libnss-stns-v2-$(VERSION)
	ln -sf $(DIST_DIR) $(STNS_DIR)/libnss-stns-v2-$(VERSION)/tmp/
	cd $(STNS_DIR) && \
		tar cf libnss-stns-v2-$(VERSION).tar libnss-stns-v2-$(VERSION) && \
		gzip -9 libnss-stns-v2-$(VERSION).tar
	cp $(STNS_DIR)/libnss-stns-v2-$(VERSION).tar.gz ./builds
	rm -rf $(STNS_DIR)

rpm: source_for_rpm ## Packaging for RPM
	@echo "$(INFO_COLOR)==> $(RESET)$(BOLD)Packaging for RPM$(RESET)"
	cp builds/libnss-stns-v2-$(VERSION).tar.gz /root/rpmbuild/SOURCES
	mkdir -p /root/rpmbuild/tmp/
	spectool -g -R rpm/stns.spec
	rpmbuild --target `uname -m` -ba rpm/stns.spec
	mv /root/rpmbuild/RPMS/*/*.rpm /stns/builds

source_for_deb: ## Create source for DEB
	@echo "$(INFO_COLOR)==> $(RESET)$(BOLD)Distributing$(RESET)"
	rm -rf $(STNS_DIR)
	mkdir -p $(STNS_DIR)/libnss-stns-v2-$(VERSION)
	cp -r $(SOURCES) $(STNS_DIR)/libnss-stns-v2-$(VERSION)
	cd $(STNS_DIR) && \
		tar cf libnss-stns-v2_$(VERSION).tar libnss-stns-v2-$(VERSION) && \
		xz -v libnss-stns-v2_$(VERSION).tar
	mv $(STNS_DIR)/libnss-stns-v2_$(VERSION).tar.xz $(STNS_DIR)/libnss-stns-v2_$(VERSION).orig.tar.xz

deb: source_for_deb ## Packaging for DEB
	@echo "$(INFO_COLOR)==> $(RESET)$(BOLD)Packaging for DEB$(RESET)"
	cd $(STNS_DIR) && \
		tar xf libnss-stns-v2_$(VERSION).orig.tar.xz && \
		cd libnss-stns-v2-$(VERSION) && \
		dh_make --single --createorig -y && \
		rm -rf debian/*.ex debian/*.EX debian/README.Debian && \
		cp -v /stns/debian/* debian/ && \
		sed -i -e 's/xenial/$(DIST)/g' debian/changelog && \
		debuild -e DIST=$(DIST) -uc -us -a`dpkg --print-architecture` 
	cd $(STNS_DIR) && \
		find . -name "*.deb" | sed -e 's/\(\(.*libnss-stns-v2.*\).deb\)/mv \1 \2.$(DIST).deb/g' | sh && \
		mkdir -p /stns/builds && \
		cp *.deb /stns/builds
	rm -rf $(STNS_DIR)


.PHONY: version
version:
	@git describe --tags --abbrev=0|sed -e 's/v//g' > version

pkg: version ## Create some distribution packages
	rm -rf builds && mkdir builds
	docker-compose build
	docker-compose up

changelog:
	git-chglog -o CHANGELOG.md

docker:
	docker rm -f libnss-stns | true
	docker rm -f httpbin | true
	docker network inspect libnss-stns || docker network create libnss-stns
	docker build -f dockerfiles/Dockerfile -t libnss_develop .
	docker run --network libnss-stns --privileged -d -e DIST=$(DIST) --name libnss-stns -v "`pwd`":/stns -it libnss_develop /sbin/init
	docker run --network libnss-stns --rm --name httpbin -d kennethreitz/httpbin
login: docker
	docker exec -it libnss-stns /bin/bash

test_on_docker: docker
	docker exec -t libnss-stns make test
	docker exec -t libnss-stns make flawfinder
	docker exec -t libnss-stns make integration

github_release: ## Create some distribution packages
	ghr -u STNS --replace v$(VERSION) builds/

parson:
	rm -rf /tmp/parson && git clone https://github.com/kgabis/parson.git /tmp/parson && \
		mv /tmp/parson/parson.h ./ && \
		mv /tmp/parson/parson.c ./
cleanup:
	rm -rf /var/cache/stns
	rm -rf /var/tmp/.stns.lock
flawfinder:
	ls stns*c |grep -v test | xargs flawfinder --error-level 3 --minlevel 3
.PHONY: test testdev build parson
