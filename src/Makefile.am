# This file is part of libkanberra.
#
# Copyright 2008 Lennart Poettering
#
# libkanberra is free software; you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License as
# published by the Free Software Foundation, either version 2.1 of the
# License, or (at your option) any later version.
#
# libkanberra is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with libkanberra. If not, see
# <http://www.gnu.org/licenses/>.

ctkmoduledir = @CTK_MODULES_DIR@
ctk3moduledir = @CTK3_MODULES_DIR@

plugindir = $(libdir)/libkanberra-@PACKAGE_VERSION@

gnomeshutdowndir = $(datadir)/gnome/shutdown

gnomeautostartdir = $(datadir)/gnome/autostart

gdmautostartdir = $(datadir)/gdm/autostart/LoginWindow

gsdctkmodulesdir = $(libdir)/gnome-settings-daemon-3.0/ctk-modules

AM_CFLAGS = $(WARNINGFLAGS) $(PTHREAD_CFLAGS) -DCA_PLUGIN_PATH=\"$(plugindir)\"
#AM_CFLAGS = $(WARNINGFLAGS) $(PTHREAD_CFLAGS) -DCA_PLUGIN_PATH=\"/home/lennart/projects/libkanberra/src/.libs\"
AM_CXXFLAGS = $(WARNINGFLAGS) $(PTHREAD_CFLAGS)
AM_LDADD = $(PTHREAD_LIBS)
AM_LDFLAGS = $(GCLDFLAGS)

EXTRA_DIST = \
	map-file

lib_LTLIBRARIES = \
        libkanberra.la

include_HEADERS = \
	kanberra.h

noinst_PROGRAMS = \
	test-kanberra

libkanberra_la_SOURCES = \
	kanberra.h \
	common.c common.h \
	mutex-posix.c mutex.h \
	proplist.c proplist.h \
	driver.h \
	read-sound-file.c read-sound-file.h \
	read-vorbis.c read-vorbis.h \
	read-wav.c read-wav.h \
	sound-theme-spec.c sound-theme-spec.h \
	llist.h \
	macro.h macro.c \
	malloc.c malloc.h \
	fork-detect.c fork-detect.h
libkanberra_la_CFLAGS = \
	$(AM_CFLAGS) \
	$(VORBIS_CFLAGS)
libkanberra_la_LIBADD = \
	$(VORBIS_LIBS)
libkanberra_la_LDFLAGS = \
	-export-dynamic \
	-version-info $(LIBKANBERRA_VERSION_INFO)

if USE_VERSION_SCRIPT
libkanberra_la_LDFLAGS += -Wl,-version-script=$(srcdir)/map-file
endif

if HAVE_CACHE

libkanberra_la_SOURCES += \
	cache.c cache.h
libkanberra_la_CFLAGS += \
	$(TDB_CLFAGS) \
	-DCA_MACHINE_ID=\"$(localstatedir)/lib/dbus/machine-id\"
libkanberra_la_LIBADD += \
	$(TDB_LIBS)

endif

plugin_LTLIBRARIES =

if BUILTIN_DSO

libkanberra_la_SOURCES += \
	dso.c \
	driver-order.c driver-order.h
libkanberra_la_LIBADD += \
	$(LIBLTDL)

plugin_LTLIBRARIES += \
	libkanberra-multi.la

libkanberra_multi_la_SOURCES = \
	multi.c
libkanberra_multi_la_CFLAGS = \
	 -Ddriver_open=multi_driver_open \
	 -Ddriver_destroy=multi_driver_destroy \
	 -Ddriver_change_device=multi_driver_change_device \
	 -Ddriver_change_props=multi_driver_change_props \
	 -Ddriver_play=multi_driver_play \
	 -Ddriver_cancel=multi_driver_cancel \
	 -Ddriver_cache=multi_driver_cache
libkanberra_multi_la_LIBADD = \
	libkanberra.la
libkanberra_multi_la_LDFLAGS = \
	-avoid-version -module -export-dynamic
endif

if HAVE_PULSE
if BUILTIN_PULSE

libkanberra_la_SOURCES += \
	pulse.c
libkanberra_la_CFLAGS += \
	$(PULSE_CFLAGS)
libkanberra_la_LIBADD += \
	$(PULSE_LIBS)

else

plugin_LTLIBRARIES += \
	libkanberra-pulse.la

libkanberra_pulse_la_SOURCES = \
	pulse.c
libkanberra_pulse_la_CFLAGS = \
	$(PULSE_CFLAGS) \
	 -Ddriver_open=pulse_driver_open \
	 -Ddriver_destroy=pulse_driver_destroy \
	 -Ddriver_change_device=pulse_driver_change_device \
	 -Ddriver_change_props=pulse_driver_change_props \
	 -Ddriver_play=pulse_driver_play \
	 -Ddriver_cancel=pulse_driver_cancel \
	 -Ddriver_cache=pulse_driver_cache
libkanberra_pulse_la_LIBADD = \
	$(PULSE_LIBS) \
	libkanberra.la
libkanberra_pulse_la_LDFLAGS = \
	-avoid-version -module -export-dynamic

endif
endif

if HAVE_ALSA
if BUILTIN_ALSA

libkanberra_la_SOURCES += \
	alsa.c
libkanberra_la_CFLAGS += \
	$(ALSA_CFLAGS)
libkanberra_la_LIBADD += \
	 $(ALSA_LIBS)

else

plugin_LTLIBRARIES += \
	libkanberra-alsa.la

libkanberra_alsa_la_SOURCES = \
	alsa.c
libkanberra_alsa_la_CFLAGS = \
	$(ALSA_CFLAGS) \
	 -Ddriver_open=alsa_driver_open \
	 -Ddriver_destroy=alsa_driver_destroy \
	 -Ddriver_change_device=alsa_driver_change_device \
	 -Ddriver_change_props=alsa_driver_change_props \
	 -Ddriver_play=alsa_driver_play \
	 -Ddriver_cancel=alsa_driver_cancel \
	 -Ddriver_cache=alsa_driver_cache
libkanberra_alsa_la_LIBADD = \
	$(ALSA_LIBS) \
	libkanberra.la
libkanberra_alsa_la_LDFLAGS = \
	-avoid-version -module -export-dynamic

endif
endif

if HAVE_OSS
if BUILTIN_OSS

libkanberra_la_SOURCES += \
	oss.c

else

plugin_LTLIBRARIES += \
	libkanberra-oss.la

libkanberra_oss_la_SOURCES = \
	oss.c
libkanberra_oss_la_CFLAGS = \
	 -Ddriver_open=oss_driver_open \
	 -Ddriver_destroy=oss_driver_destroy \
	 -Ddriver_change_device=oss_driver_change_device \
	 -Ddriver_change_props=oss_driver_change_props \
	 -Ddriver_play=oss_driver_play \
	 -Ddriver_cancel=oss_driver_cancel \
	 -Ddriver_cache=oss_driver_cache
libkanberra_oss_la_LIBADD = \
	libkanberra.la
libkanberra_oss_la_LDFLAGS = \
	-avoid-version -module -export-dynamic
endif
endif

if HAVE_GSTREAMER
if BUILTIN_GSTREAMER

libkanberra_la_SOURCES += \
	gstreamer.c
libkanberra_la_CFLAGS += \
	$(GST_CFLAGS)
libkanberra_la_LIBADD += \
	 $(GST_LIBS)

else

plugin_LTLIBRARIES += \
	libkanberra-gstreamer.la

libkanberra_gstreamer_la_SOURCES = \
	gstreamer.c
libkanberra_gstreamer_la_CFLAGS = \
	$(GST_CFLAGS) \
	 -Ddriver_open=gstreamer_driver_open \
	 -Ddriver_destroy=gstreamer_driver_destroy \
	 -Ddriver_change_device=gstreamer_driver_change_device \
	 -Ddriver_change_props=gstreamer_driver_change_props \
	 -Ddriver_play=gstreamer_driver_play \
	 -Ddriver_cancel=gstreamer_driver_cancel \
	 -Ddriver_cache=gstreamer_driver_cache
libkanberra_gstreamer_la_LIBADD = \
	$(GST_LIBS) \
	libkanberra.la
libkanberra_gstreamer_la_LDFLAGS = \
	-avoid-version -module -export-dynamic

endif
endif

if HAVE_NULL
if BUILTIN_NULL

libkanberra_la_SOURCES += \
	null.c

else

plugin_LTLIBRARIES += \
	libkanberra-null.la

libkanberra_null_la_SOURCES = \
	null.c
libkanberra_null_la_CFLAGS = \
	 -Ddriver_open=null_driver_open \
	 -Ddriver_destroy=null_driver_destroy \
	 -Ddriver_change_device=null_driver_change_device \
	 -Ddriver_change_props=null_driver_change_props \
	 -Ddriver_play=null_driver_play \
	 -Ddriver_cancel=null_driver_cancel \
	 -Ddriver_cache=null_driver_cache
libkanberra_null_la_LIBADD = \
	libkanberra.la
libkanberra_null_la_LDFLAGS = \
	-avoid-version -module -export-dynamic
endif
endif

bin_PROGRAMS =
CLEANFILES =

if HAVE_UDEV
if HAVE_ALSA

bin_PROGRAMS += \
	kanberra-boot

kanberra_boot_SOURCES = \
	kanberra-boot.c

kanberra_boot_LDADD = \
	$(UDEV_LIBS) \
	libkanberra.la

kanberra_boot_CFLAGS = \
	$(UDEV_CFLAGS)

if HAVE_SYSTEMD

systemdsystemunit_DATA = \
	kanberra-system-bootup.service \
	kanberra-system-shutdown.service \
	kanberra-system-shutdown-reboot.service

EXTRA_DIST += \
	kanberra-system-bootup.service.in \
	kanberra-system-shutdown.service.in \
	kanberra-system-shutdown-reboot.service.in

%.service: %.service.in
	$(AM_V_GEN)sed -e 's,@bindir\@,$(bindir),g' $< > $@

CLEANFILES += \
	$(systemdsystemunit_DATA)

endif
endif
endif

if HAVE_CTK_ANY

bin_PROGRAMS += \
	kanberra-ctk-play

include_HEADERS += \
	kanberra-ctk.h

endif

if HAVE_CTK3

lib_LTLIBRARIES += \
	libkanberra-ctk3.la

ctk3module_LTLIBRARIES = \
	libkanberra-ctk3-module.la

libkanberra_ctk3_la_SOURCES = \
	kanberra-ctk.h \
	kanberra-ctk.c
libkanberra_ctk3_la_CFLAGS = \
	$(CTK3_CFLAGS)
libkanberra_ctk3_la_LIBADD = \
	$(CTK3_LIBS) \
	-lX11 \
	libkanberra.la
libkanberra_ctk3_la_LDFLAGS = \
	-export-dynamic -version-info $(LIBKANBERRA_CTK_VERSION_INFO)

libkanberra_ctk3_module_la_SOURCES = \
	kanberra-ctk-module.c
libkanberra_ctk3_module_la_CFLAGS = \
	$(CTK3_CFLAGS)
libkanberra_ctk3_module_la_LIBADD = \
	$(CTK3_LIBS) \
	-lX11 \
	-lgmodule-2.0 \
	libkanberra.la \
	libkanberra-ctk3.la
libkanberra_ctk3_module_la_LDFLAGS = \
	-avoid-version -module -export-dynamic

install-exec-hook:
	$(MKDIR_P) -m 755 $(DESTDIR)$(ctk3moduledir)
	( cd $(DESTDIR)$(ctk3moduledir) && \
		rm -f libkanberra-ctk-module.so && \
	        $(LN_S) libkanberra-ctk3-module.so libkanberra-ctk-module.so )

endif

if HAVE_CTK

lib_LTLIBRARIES += \
	libkanberra-ctk.la

ctkmodule_LTLIBRARIES = \
	libkanberra-ctk-module.la

libkanberra_ctk_la_SOURCES = \
	kanberra-ctk.h \
	kanberra-ctk.c
libkanberra_ctk_la_CFLAGS = \
	$(CTK_CFLAGS)
libkanberra_ctk_la_LIBADD = \
	$(CTK_LIBS) \
	-lX11 \
	libkanberra.la
libkanberra_ctk_la_LDFLAGS = \
	-export-dynamic -version-info $(LIBKANBERRA_CTK_VERSION_INFO)

libkanberra_ctk_module_la_SOURCES = \
	kanberra-ctk-module.c
libkanberra_ctk_module_la_CFLAGS = \
	$(CTK_CFLAGS)
libkanberra_ctk_module_la_LIBADD = \
	$(CTK_LIBS) \
	-lX11 \
	-lgmodule-2.0 \
	libkanberra.la \
	libkanberra-ctk.la
libkanberra_ctk_module_la_LDFLAGS = \
	-avoid-version -module -export-dynamic

endif

if HAVE_CTK3

kanberra_ctk_play_LDADD = \
	$(CTK3_LIBS) \
	libkanberra.la \
	libkanberra-ctk3.la
kanberra_ctk_play_CFLAGS = \
	$(CTK3_CFLAGS)

else
if HAVE_CTK

kanberra_ctk_play_LDADD = \
	$(CTK_LIBS) \
	libkanberra.la \
	libkanberra-ctk.la
kanberra_ctk_play_CFLAGS = \
	$(CTK_CFLAGS)

endif
endif

if HAVE_CTK_ANY

dist_gsdctkmodules_DATA = \
	kanberra-ctk-module.desktop

kanberra_ctk_play_SOURCES = \
	kanberra-ctk-play.c

EXTRA_DIST += \
	libkanberra-login-sound.desktop.in \
	libkanberra-ready-sound.desktop.in \
	libkanberra-logout-sound.sh.in

gnomeautostart_DATA = \
	libkanberra-login-sound.desktop

gdmautostart_DATA = \
	libkanberra-ready-sound.desktop

gnomeshutdown_SCRIPTS = \
	libkanberra-logout-sound.sh

CLEANFILES += \
	libkanberra-login-sound.desktop \
	libkanberra-ready-sound.desktop \
	libkanberra-logout-sound.sh

libkanberra-logout-sound.sh: libkanberra-logout-sound.sh.in Makefile
	$(AM_V_GEN)sed -e 's,@bindir\@,$(bindir),g' < $< > $@ && \
		chmod +x $@

libkanberra-login-sound.desktop: libkanberra-login-sound.desktop.in Makefile
	$(AM_V_GEN)sed -e 's,@bindir\@,$(bindir),g' < $< > $@

libkanberra-ready-sound.desktop: libkanberra-ready-sound.desktop.in Makefile
	$(AM_V_GEN)sed -e 's,@bindir\@,$(bindir),g' < $< > $@

endif

test_kanberra_SOURCES = \
        test-kanberra.c
test_kanberra_LDADD = \
        $(AM_LDADD) \
        libkanberra.la
