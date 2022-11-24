# -*- mode: makefile -*-

####################################
# Everything below here is generic #
####################################

if CTK_DOC_USE_LIBTOOL
CTKDOC_CC = $(LIBTOOL) --tag=CC --mode=compile $(CC) $(INCLUDES) $(CTKDOC_DEPS_CFLAGS) $(AM_CPPFLAGS) $(CPPFLAGS) $(AM_CFLAGS) $(CFLAGS)
CTKDOC_LD = $(LIBTOOL) --tag=CC --mode=link $(CC) $(CTKDOC_DEPS_LIBS) $(AM_CFLAGS) $(CFLAGS) $(AM_LDFLAGS) $(LDFLAGS)
CTKDOC_RUN = $(LIBTOOL) --mode=execute
else
CTKDOC_CC = $(CC) $(INCLUDES) $(CTKDOC_DEPS_CFLAGS) $(AM_CPPFLAGS) $(CPPFLAGS) $(AM_CFLAGS) $(CFLAGS)
CTKDOC_LD = $(CC) $(CTKDOC_DEPS_LIBS) $(AM_CFLAGS) $(CFLAGS) $(AM_LDFLAGS) $(LDFLAGS)
CTKDOC_RUN =
endif

# We set GPATH here; this gives us semantics for GNU make
# which are more like other make's VPATH, when it comes to
# whether a source that is a target of one rule is then
# searched for in VPATH/GPATH.
#
GPATH = $(srcdir)

TARGET_DIR=$(HTML_DIR)/$(DOC_MODULE)

SETUP_FILES = \
	$(content_files)		\
	$(DOC_MAIN_SGML_FILE)		\
	$(DOC_MODULE)-sections.txt	\
	$(DOC_MODULE)-overrides.txt

EXTRA_DIST = 				\
	$(HTML_IMAGES)			\
	$(SETUP_FILES)

DOC_STAMPS=setup-build.stamp scan-build.stamp tmpl-build.stamp sgml-build.stamp \
	html-build.stamp pdf-build.stamp \
	tmpl.stamp sgml.stamp html.stamp pdf.stamp

SCANOBJ_FILES = 		 \
	$(DOC_MODULE).args 	 \
	$(DOC_MODULE).hierarchy  \
	$(DOC_MODULE).interfaces \
	$(DOC_MODULE).prerequisites \
	$(DOC_MODULE).signals

REPORT_FILES = \
	$(DOC_MODULE)-undocumented.txt \
	$(DOC_MODULE)-undeclared.txt \
	$(DOC_MODULE)-unused.txt

CLEANFILES = $(SCANOBJ_FILES) $(REPORT_FILES) $(DOC_STAMPS)

if ENABLE_CTK_DOC
if CTK_DOC_BUILD_HTML
HTML_BUILD_STAMP=html-build.stamp
else
HTML_BUILD_STAMP=
endif
if CTK_DOC_BUILD_PDF
PDF_BUILD_STAMP=pdf-build.stamp
else
PDF_BUILD_STAMP=
endif

all-local: $(HTML_BUILD_STAMP) $(PDF_BUILD_STAMP)
else
all-local:
endif

docs: $(HTML_BUILD_STAMP) $(PDF_BUILD_STAMP)

$(REPORT_FILES): sgml-build.stamp

#### setup ####

setup-build.stamp:
	-@if test "$(abs_srcdir)" != "$(abs_builddir)" ; then \
	    echo '  DOC   Preparing build'; \
	    files=`echo $(SETUP_FILES) $(expand_content_files) $(DOC_MODULE).types`; \
	    if test "x$$files" != "x" ; then \
	        for file in $$files ; do \
	            test -f $(abs_srcdir)/$$file && \
	                cp -pu $(abs_srcdir)/$$file $(abs_builddir)/ || true; \
	        done; \
	    fi; \
	    test -d $(abs_srcdir)/tmpl && \
	        { cp -rp $(abs_srcdir)/tmpl $(abs_builddir)/; \
	        chmod -R u+w $(abs_builddir)/tmpl; } \
	fi
	@touch setup-build.stamp

#### scan ####

scan-build.stamp: $(HFILE_GLOB) $(CFILE_GLOB)
	@echo '  DOC   Scanning header files'
	@_source_dir='' ; \
	for i in $(DOC_SOURCE_DIR) ; do \
	    _source_dir="$${_source_dir} --source-dir=$$i" ; \
	done ; \
	ctkdoc-scan --module=$(DOC_MODULE) --ignore-headers="$(IGNORE_HFILES)" $${_source_dir} $(SCAN_OPTIONS) $(EXTRA_HFILES)
	@if grep -l '^..*$$' $(DOC_MODULE).types > /dev/null 2>&1 ; then \
	    echo "  DOC   Introspecting gobjects"; \
	    scanobj_options=""; \
	    ctkdoc-scangobj 2>&1 --help | grep  >/dev/null "\-\-verbose"; \
	    if test "$(?)" = "0"; then \
	        if test "x$(V)" = "x1"; then \
	            scanobj_options="--verbose"; \
	        fi; \
	    fi; \
	    CC="$(CTKDOC_CC)" LD="$(CTKDOC_LD)" RUN="$(CTKDOC_RUN)" CFLAGS="$(CTKDOC_CFLAGS) $(CFLAGS)" LDFLAGS="$(CTKDOC_LIBS) $(LDFLAGS)" \
	    ctkdoc-scangobj $(SCANGOBJ_OPTIONS) $$scanobj_options --module=$(DOC_MODULE); \
	else \
	    for i in $(SCANOBJ_FILES) ; do \
	        test -f $$i || touch $$i ; \
	    done \
	fi
	@touch scan-build.stamp

$(DOC_MODULE)-decl.txt $(SCANOBJ_FILES) $(DOC_MODULE)-sections.txt $(DOC_MODULE)-overrides.txt: scan-build.stamp
	@true

#### templates ####

tmpl-build.stamp: setup-build.stamp $(DOC_MODULE)-decl.txt $(SCANOBJ_FILES) $(DOC_MODULE)-sections.txt $(DOC_MODULE)-overrides.txt
	@echo '  DOC   Rebuilding template files'
	@ctkdoc-mktmpl --module=$(DOC_MODULE) $(MKTMPL_OPTIONS)
	@if test "$(abs_srcdir)" != "$(abs_builddir)" ; then \
	  if test -w $(abs_srcdir) ; then \
	    cp -rp $(abs_builddir)/tmpl $(abs_srcdir)/; \
	  fi \
	fi
	@touch tmpl-build.stamp

tmpl.stamp: tmpl-build.stamp
	@true

$(srcdir)/tmpl/*.sgml:
	@true

#### xml ####

sgml-build.stamp: tmpl.stamp $(DOC_MODULE)-sections.txt $(srcdir)/tmpl/*.sgml $(expand_content_files)
	@echo '  DOC   Building XML'
	@-chmod -R u+w $(srcdir)
	@_source_dir='' ; \
	for i in $(DOC_SOURCE_DIR) ; do \
	    _source_dir="$${_source_dir} --source-dir=$$i" ; \
	done ; \
	ctkdoc-mkdb --module=$(DOC_MODULE) --output-format=xml --expand-content-files="$(expand_content_files)" --main-sgml-file=$(DOC_MAIN_SGML_FILE) $${_source_dir} $(MKDB_OPTIONS)
	@touch sgml-build.stamp

sgml.stamp: sgml-build.stamp
	@true

#### html ####

html-build.stamp: sgml.stamp $(DOC_MAIN_SGML_FILE) $(content_files)
	@echo '  DOC   Building HTML'
	@rm -rf html
	@mkdir html
	@mkhtml_options=""; \
	ctkdoc-mkhtml 2>&1 --help | grep  >/dev/null "\-\-verbose"; \
	if test "$(?)" = "0"; then \
	  if test "x$(V)" = "x1"; then \
	    mkhtml_options="$$mkhtml_options --verbose"; \
	  fi; \
	fi; \
	ctkdoc-mkhtml 2>&1 --help | grep  >/dev/null "\-\-path"; \
	if test "$(?)" = "0"; then \
	  mkhtml_options="$$mkhtml_options --path=\"$(abs_srcdir)\""; \
	fi; \
	cd html && ctkdoc-mkhtml $$mkhtml_options $(MKHTML_OPTIONS) $(DOC_MODULE) ../$(DOC_MAIN_SGML_FILE)
	-@test "x$(HTML_IMAGES)" = "x" || \
	for file in $(HTML_IMAGES) ; do \
	  if test -f $(abs_srcdir)/$$file ; then \
	    cp $(abs_srcdir)/$$file $(abs_builddir)/html; \
	  fi; \
	  if test -f $(abs_builddir)/$$file ; then \
	    cp $(abs_builddir)/$$file $(abs_builddir)/html; \
	  fi; \
	done;
	@echo '  DOC   Fixing cross-references'
	@ctkdoc-fixxref --module=$(DOC_MODULE) --module-dir=html --html-dir=$(HTML_DIR) $(FIXXREF_OPTIONS)
	@touch html-build.stamp

#### pdf ####

pdf-build.stamp: sgml.stamp $(DOC_MAIN_SGML_FILE) $(content_files)
	@echo '  DOC   Building PDF'
	@rm -f $(DOC_MODULE).pdf
	@mkpdf_options=""; \
	ctkdoc-mkpdf 2>&1 --help | grep  >/dev/null "\-\-verbose"; \
	if test "$(?)" = "0"; then \
	  if test "x$(V)" = "x1"; then \
	    mkpdf_options="$$mkpdf_options --verbose"; \
	  fi; \
	fi; \
	if test "x$(HTML_IMAGES)" != "x"; then \
	  for img in $(HTML_IMAGES); do \
	    part=`dirname $$img`; \
	    echo $$mkpdf_options | grep >/dev/null "\-\-imgdir=$$part "; \
	    if test $$? != 0; then \
	      mkpdf_options="$$mkpdf_options --imgdir=$$part"; \
	    fi; \
	  done; \
	fi; \
	ctkdoc-mkpdf --path="$(abs_srcdir)" $$mkpdf_options $(DOC_MODULE) $(DOC_MAIN_SGML_FILE) $(MKPDF_OPTIONS)
	@touch pdf-build.stamp

##############

clean-local:
	@rm -f *~ *.bak
	@rm -rf .libs

distclean-local:
	@rm -rf xml html $(REPORT_FILES) $(DOC_MODULE).pdf \
	    $(DOC_MODULE)-decl-list.txt $(DOC_MODULE)-decl.txt
	@if test "$(abs_srcdir)" != "$(abs_builddir)" ; then \
	    rm -f $(SETUP_FILES) $(expand_content_files) $(DOC_MODULE).types; \
	    rm -rf tmpl; \
	fi

maintainer-clean-local: clean
	@rm -rf xml html

install-data-local:
	@installfiles=`echo $(builddir)/html/*`; \
	if test "$$installfiles" = '$(builddir)/html/*'; \
	then echo 1>&2 'Nothing to install' ; \
	else \
	  if test -n "$(DOC_MODULE_VERSION)"; then \
	    installdir="$(DESTDIR)$(TARGET_DIR)-$(DOC_MODULE_VERSION)"; \
	  else \
	    installdir="$(DESTDIR)$(TARGET_DIR)"; \
	  fi; \
	  $(mkinstalldirs) $${installdir} ; \
	  for i in $$installfiles; do \
	    echo ' $(INSTALL_DATA) '$$i ; \
	    $(INSTALL_DATA) $$i $${installdir}; \
	  done; \
	  if test -n "$(DOC_MODULE_VERSION)"; then \
	    mv -f $${installdir}/$(DOC_MODULE).devhelp2 \
	      $${installdir}/$(DOC_MODULE)-$(DOC_MODULE_VERSION).devhelp2; \
	  fi; \
	  $(CTKDOC_REBASE) --relative --dest-dir=$(DESTDIR) --html-dir=$${installdir}; \
	fi

uninstall-local:
	@if test -n "$(DOC_MODULE_VERSION)"; then \
	  installdir="$(DESTDIR)$(TARGET_DIR)-$(DOC_MODULE_VERSION)"; \
	else \
	  installdir="$(DESTDIR)$(TARGET_DIR)"; \
	fi; \
	rm -rf $${installdir}

#
# Require ctk-doc when making dist
#
if ENABLE_CTK_DOC
dist-check-ctkdoc:
else
dist-check-ctkdoc:
	@echo "*** ctk-doc must be installed and enabled in order to make dist"
	@false
endif

dist-hook: dist-check-ctkdoc dist-hook-local
	@mkdir $(distdir)/tmpl
	@mkdir $(distdir)/html
	@-cp ./tmpl/*.sgml $(distdir)/tmpl
	@cp ./html/* $(distdir)/html
	@-cp ./$(DOC_MODULE).pdf $(distdir)/
	@-cp ./$(DOC_MODULE).types $(distdir)/
	@-cp ./$(DOC_MODULE)-sections.txt $(distdir)/
	@cd $(distdir) && rm -f $(DISTCLEANFILES)
	@$(CTKDOC_REBASE) --online --relative --html-dir=$(distdir)/html

.PHONY : dist-hook-local docs
