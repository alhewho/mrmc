include ../../Makefile.include
DEPS= ../../Makefile.include 03-gcrypt-android-select.patch Makefile

# lib name, version
LIBNAME=libgcrypt
VERSION=1.7.3
SOURCE=$(LIBNAME)-$(VERSION)
ARCHIVE=$(SOURCE).tar.bz2
BASE_URL=ftp://ftp.gnupg.org/gcrypt/libgcrypt

ifeq ($(OS),osx)
CONFIGURE_FLAGS+= --disable-asm --disable-avx-support --disable-avx2-support
endif
ifeq ($(CPU),arm64)
CONFIGURE_FLAGS+= --disable-asm --disable-neon-support
endif

# configuration settings
CONFIGURE=cp -f $(CONFIG_SUB) $(CONFIG_GUESS) .; \
          ./configure --prefix=$(PREFIX) --disable-shared \
	  $(CONFIGURE_FLAGS)

LIBDYLIB=$(PLATFORM)/src/.libs/$(LIBNAME).a

CLEAN_FILES=$(ARCHIVE) $(PLATFORM)

all: .installed-$(PLATFORM)

$(TARBALLS_LOCATION)/$(ARCHIVE):
	cd $(TARBALLS_LOCATION); $(RETRIEVE_TOOL) $(RETRIEVE_TOOL_FLAGS) $(BASE_URL)/$(ARCHIVE)

$(PLATFORM): $(TARBALLS_LOCATION)/$(ARCHIVE) $(DEPS)
	rm -rf $(PLATFORM)/*; mkdir -p $(PLATFORM)
	cd $(PLATFORM); $(ARCHIVE_TOOL) $(ARCHIVE_TOOL_FLAGS) $(TARBALLS_LOCATION)/$(ARCHIVE)
	cd $(PLATFORM); patch -p0 < ../03-gcrypt-android-select.patch
	#cd $(PLATFORM); patch -p0 < ../04-oflagmunging.patch
	# do not build the tests or docs
	sed -ie "s| tests||" "$(PLATFORM)/Makefile.am"
	sed -ie "s|serial-tests ||" "$(PLATFORM)/configure.ac"
	cp config.sub $(PLATFORM)/config.sub
	cd $(PLATFORM); $(AUTORECONF) -vif
	cd $(PLATFORM); $(CONFIGURE)

$(LIBDYLIB): $(PLATFORM)
	$(MAKE) -C $(PLATFORM)

.installed-$(PLATFORM): $(LIBDYLIB)
	$(MAKE) -C $(PLATFORM) install
	touch $@

clean:
	$(MAKE) -C $(PLATFORM) clean
	rm -f .installed-$(PLATFORM)

distclean::
	rm -rf $(PLATFORM) .installed-$(PLATFORM)
