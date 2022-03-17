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
LIBDIR=$(PREFIX)/lib64
ifeq ($(wildcard $(LIBDIR)/.*),)
LIBDIR=$(PREFIX)/lib
endif
BINDIR=$(PREFIX)/lib/stns
BINSYMDIR=$(PREFIX)/local/bin/


CRITERION_VERSION=2.3.2
SHUNIT_VERSION=2.1.6
CURL_VERSION=7.71.1
OPENSSL_VERSION=1.1.1g
ZLIB_VERSION=1.2.11

DIST ?= unknown
STNSD_VERSION=0.0.1

DIST_DIR:=/stns/tmp/$(DIST)
SRC_DIR:=$(DIST_DIR)/src
STNS_DIR:=$(DIST_DIR)/stns
OPENSSL_DIR:=$(DIST_DIR)/openssl-$(OPENSSL_VERSION)
CURL_DIR:=$(DIST_DIR)/curl-$(CURL_VERSION)
ZLIB_DIR:=$(DIST_DIR)/zlib-$(ZLIB_VERSION)
SOURCES=Makefile stns.h stns.c stns*.c stns*.h toml.h toml.c parson.h parson.c stns.conf.example test libstns.map

STATIC_LIBS=$(CURL_DIR)/lib/libcurl.a $(OPENSSL_DIR)/lib/libssl.a  $(OPENSSL_DIR)/lib/libcrypto.a $(ZLIB_DIR)/lib/libz.a

LIBS_CFLAGS=-Os -fPIC
CURL_LDFLAGS := -L$(OPENSSL_DIR)/lib $(LIBS_CFLAGS)

MAKE=make -j4
default: build
ci: curl test integration
test: testdev ## Test with dependencies installation
	@echo "$(INFO_COLOR)==> $(RESET)$(BOLD)Testing$(RESET)"
	mkdir -p /etc/stns/client/
	echo 'api_endpoint = "https://httpbin.org"' > /etc/stns/client/stns.conf
	service cache-stnsd restart
	$(CC) -g3 -fsanitize=address -O0 -fno-omit-frame-pointer -I$(CURL_DIR)/include \
	  stns.c stns_group.c toml.c parson.c stns_shadow.c stns_passwd.c stns_test.c stns_group_test.c stns_shadow_test.c stns_passwd_test.c \
		$(STATIC_LIBS) \
		-lcriterion \
		-lpthread \
		-ldl \
		-lrt \
		-o $(DIST_DIR)/test
		$(DIST_DIR)/test --verbose

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
	  --with-libssl-prefix=$(OPENSSL_DIR) \
	  --with-zlib=$(ZLIB_DIR) \
	  --enable-libcurl-option \
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
	  --disable-smb \
	  --without-libidn && $(MAKE) && $(MAKE) install)

criterion:  ## Installing dependencies for development
	test -f $(DIST_DIR)/criterion.tar.bz2 || curl -sL https://github.com/Snaipe/Criterion/releases/download/v$(CRITERION_VERSION)/criterion-v$(CRITERION_VERSION)-linux-x86_64.tar.bz2 -o $(DIST_DIR)/criterion.tar.bz2
	test -d /usr/include/criterion || cd $(DIST_DIR); tar xf criterion.tar.bz2; cd ../
	test -d /usr/include/criterion || (mv $(DIST_DIR)/criterion-v$(CRITERION_VERSION)/include/criterion /usr/include/criterion && mv $(DIST_DIR)/criterion-v$(CRITERION_VERSION)/lib/libcriterion.* $(LIBDIR)/)
	test -f $(DIST_DIR)/shunit2.tgz || curl -sL https://storage.googleapis.com/google-code-archive-downloads/v2/code.google.com/shunit2/shunit2-$(SHUNIT_VERSION).tgz -o $(DIST_DIR)/shunit2.tgz
	cd $(DIST_DIR); tar xf shunit2.tgz; cd ../
	test -d /usr/include/shunit2 || mv $(DIST_DIR)/shunit2-$(SHUNIT_VERSION)/ /usr/include/shunit2

debug:
	@echo "$(INFO_COLOR)==> $(RESET)$(BOLD)Testing$(RESET)"
	$(CC) -g -I$(CURL_DIR)/include \
	  test/debug.c stns.c stns_group.c toml.c parson.c stns_shadow.c stns_passwd.c \
		$(STATIC_LIBS) \
		 -lpthread -ldl -o $(DIST_DIR)/debug && \
		$(DIST_DIR)/debug && valgrind --leak-check=full tmp/libs/debug

testdev: build_dir curl criterion stnsd  ## Test without dependencies installation

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

integration: testdev build install ## Run integration test
	@echo "$(INFO_COLOR)==> $(RESET)$(BOLD)Integration Testing$(RESET)"
	mkdir -p /etc/stns/client
	mkdir -p /etc/stns/server
	cp test/integration_client.conf /etc/stns/client/stns.conf && service cache-stnsd restart
	cp test/integration_server.conf /etc/stns/server/stns.conf && service stns restart
	bash -l -c "while ! nc -vz -w 1 127.0.0.1 1104 > /dev/null 2>&1; do sleep 1; echo 'sleeping'; done"
	test -d /usr/lib/x86_64-linux-gnu && ln -sf /usr/lib/libnss_stns.so.2.0 /usr/lib/x86_64-linux-gnu/libnss_stns.so.2.0 || true
	sed -i -e 's/^passwd:.*/passwd: files stns/g' /etc/nsswitch.conf
	sed -i -e 's/^shadow:.*/shadow: files stns/g' /etc/nsswitch.conf
	sed -i -e 's/^group:.*/group: files stns/g' /etc/nsswitch.conf
	grep test /etc/sudoers || echo 'test ALL=(ALL) NOPASSWD:ALL' >> /etc/sudoers
	test/integration_test.sh
	echo "use_cached = true" >> /etc/stns/client/stns.conf && service cache-stnsd restart
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
	rpmbuild -ba rpm/stns.spec
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
		debuild -e DIST=$(DIST) -uc -us
	cd $(STNS_DIR) && \
		find . -name "*.deb" | sed -e 's/\(\(.*libnss-stns-v2.*\).deb\)/mv \1 \2.$(DIST).deb/g' | sh && \
		mkdir -p /stns/builds && \
		cp *.deb /stns/builds
	rm -rf $(STNS_DIR)


SUPPORTOS=centos6 centos7 centos8 ubuntu16 ubuntu18 ubuntu20 debian8 debian9 debian10 debian11
pkg: ## Create some distribution packages
	rm -rf builds && mkdir builds
	for i in $(SUPPORTOS); do \
	  docker-compose build nss_$$i; \
	  docker-compose run --rm -v `pwd`:/stns nss_$$i; \
	done

changelog:
	git-chglog -o CHANGELOG.md

docker:
	docker rm -f libnss-stns | true
	docker build -f dockerfiles/Dockerfile -t libnss_develop .
	docker run --privileged -d -e DIST=$(DIST) --name libnss-stns -v "`pwd`":/stns -it libnss_develop /sbin/init
login: docker
	docker exec -it libnss-stns /bin/bash

test_on_docker: docker
	docker exec -t libnss-stns make test
	docker exec -t libnss-stns make integration

github_release: ## Create some distribution packages
	ghr -u STNS --replace v$(VERSION) builds/

stnsd:
	! test -e /etc/lsb-release || (! (dpkg -l |grep stnsd) && \
	  curl -s -L -O https://github.com/STNS/cache-stnsd/releases/download/v$(STNSD_VERSION)/cache-stnsd_$(STNSD_VERSION)-1_amd64.xenial.deb && \
	  dpkg -i cache-stnsd_$(STNSD_VERSION)-1_amd64.xenial.deb) | true
	! test -e /etc/redhat-release || (! (rpm -qa |grep stnsd) && \
	  curl -s -L -O https://github.com/STNS/cache-stnsd/releases/download/v$(STNSD_VERSION)/cache-stnsd-$(STNSD_VERSION)-1.x86_64.el8.rpm && \
	  rpm -ivh cache-stnsd-$(STNSD_VERSION)-1.x86_64.el8.rpm) | true
	service cache-stnsd start

.PHONY: test testdev build
