################################################################################
#
# madplay
#
################################################################################

MADPLAY_VERSION = 0.15.2b
MADPLAY_SITE = http://downloads.sourceforge.net/project/mad/madplay/$(MADPLAY_VERSION)
MADPLAY_LICENSE = GPL-2.0+
MADPLAY_LICENSE_FILES = COPYING COPYRIGHT
MADPLAY_LIBTOOL_PATCH = NO
MADPLAY_DEPENDENCIES = libmad libid3tag $(TARGET_NLS_DEPENDENCIES)

# Check if ALSA is built, then we should configure after alsa-lib so
# ./configure can find alsa-lib.
ifeq ($(BR2_PACKAGE_MADPLAY_ALSA),y)
MADPLAY_CONF_OPTS += --with-alsa
MADPLAY_DEPENDENCIES += host-pkgconf alsa-lib
MADPLAY_CONF_ENV += LIBS="`$(PKG_CONFIG_HOST_BINARY) --libs alsa`"
endif

$(eval $(autotools-package))
