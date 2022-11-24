## Process this file with automake to produce Makefile.in

# We require automake 1.6 at least.
AUTOMAKE_OPTIONS = 1.6

# This is a blank Makefile.am for using ctk-doc.
# Copy this to your project's API docs directory and modify the variables to
# suit your project. See the CTK+ Makefiles in ctk+/docs/reference for examples
# of using the various options.

# The name of the module, e.g. 'glib'.
DOC_MODULE=libcanberra

# The top-level SGML file. You can change this if you want to.
DOC_MAIN_SGML_FILE=$(DOC_MODULE)-docs.sgml

# The directory containing the source code. Relative to $(srcdir).
# ctk-doc will search all .c & .h files beneath here for inline comments
# documenting the functions and macros.
# e.g. DOC_SOURCE_DIR=../../../ctk
DOC_SOURCE_DIR=../src

# Extra options to pass to ctkdoc-scangobj. Not normally needed.
SCANGOBJ_OPTIONS=

# Extra options to supply to ctkdoc-scan.
# e.g. SCAN_OPTIONS=--deprecated-guards="CTK_DISABLE_DEPRECATED"
SCAN_OPTIONS=

# Extra options to supply to ctkdoc-mkdb.
# e.g. MKDB_OPTIONS=--sgml-mode --output-format=xml
MKDB_OPTIONS=--sgml-mode --output-format=xml

# Extra options to supply to ctkdoc-mktmpl
# e.g. MKTMPL_OPTIONS=--only-section-tmpl
MKTMPL_OPTIONS=

# Extra options to supply to ctkdoc-fixref. Not normally needed.
# e.g. FIXXREF_OPTIONS=--extra-dir=../gdk-pixbuf/html --extra-dir=../gdk/html
FIXXREF_OPTIONS=

# Used for dependencies. The docs will be rebuilt if any of these change.
# e.g. HFILE_GLOB=$(top_srcdir)/ctk/*.h
# e.g. CFILE_GLOB=$(top_srcdir)/ctk/*.c
HFILE_GLOB=
CFILE_GLOB=

# Header files to ignore when scanning.
# e.g. IGNORE_HFILES=ctkdebug.h ctkintl.h
IGNORE_HFILES=

# Images to copy into HTML directory.
# e.g. HTML_IMAGES=$(top_srcdir)/ctk/stock-icons/stock_about_24.png
HTML_IMAGES=

# Extra SGML files that are included by $(DOC_MAIN_SGML_FILE).
# e.g. content_files=running.sgml building.sgml changes-2.0.sgml
content_files=

# SGML files where ctk-doc abbrevations (#CtkWidget) are expanded
# These files must be listed here *and* in content_files
# e.g. expand_content_files=running.sgml
expand_content_files=

# CFLAGS and LDFLAGS for compiling ctkdoc-scangobj with your library.
# Only needed if you are using ctkdoc-scangobj to dynamically query widget
# signals and properties.
# e.g. INCLUDES=-I$(top_srcdir) -I$(top_builddir) $(CTK_DEBUG_FLAGS)
# e.g. CTKDOC_LIBS=$(top_builddir)/ctk/$(ctktargetlib)
INCLUDES=
CTKDOC_LIBS=

# This includes the standard ctk-doc make rules, copied by ctkdocize.
include $(top_srcdir)/ctkdoc/ctk-doc.make

# Other files to distribute
# e.g. EXTRA_DIST += version.xml.in
EXTRA_DIST +=

# Files not to distribute
# for --rebuild-types in $(SCAN_OPTIONS), e.g. $(DOC_MODULE).types
# for --rebuild-sections in $(SCAN_OPTIONS) e.g. $(DOC_MODULE)-sections.txt
#DISTCLEANFILES +=

# Comment this out if you want your docs-status tested during 'make check'
#TESTS = $(CTKDOC_CHECK)

