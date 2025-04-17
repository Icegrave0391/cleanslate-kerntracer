# Makefile.am --- automake input file

AUTOMAKE_OPTIONS = subdir-objects
install-exec-local:
	for f in stap-jupyter-container stap-jupyter-install; \
		do $(INSTALL_PROGRAM) -D $$f $(DESTDIR)$(bindir)/$$f; done

install-data-local:
	(cd "$(srcdir)"; for f in *.{ipynb,md,txt,json,py}; \
		do $(INSTALL_DATA) -D $$f $(DESTDIR)$(pkgdatadir)/interactive-notebook/$$f; done)
	(cd "$(srcdir)/isystemtap"; for f in *.{py,json}; \
		do $(INSTALL_DATA) -D $$f $(DESTDIR)$(pkgdatadir)/interactive-notebook/isystemtap/$$f; done)
	(cd "$(srcdir)/container"; for f in *; \
		do $(INSTALL_DATA) -D $$f $(DESTDIR)$(pkgdatadir)/interactive-notebook/container/$$f; done)
	($(INSTALL_DATA) -D -m 755 stap-jupyter-install $(DESTDIR)$(pkgdatadir)/interactive-notebook/container/stap-jupyter-install)
	(cd "$(srcdir)/codemirror"; \
		for f in jupyterlab-stap-highlighting/__init__.py lib/plugin.js lib/stap.js package.json; \
		do $(INSTALL_DATA) -D $$f $(DESTDIR)$(pkgdatadir)/interactive-notebook/codemirror/$$f; done)
	(cd "$(srcdir)/tests"; for f in *; \
		do $(INSTALL_DATA) -D $$f $(DESTDIR)$(pkgdatadir)/interactive-notebook/tests/$$f; done)
	@echo "STAP_VERSION    = '$(VERSION)'"               >  "constants.py"
	@echo "STAP_PKGDATADIR = '$(pkgdatadir)'"            >> "constants.py"
	@echo "STAP_PREFIX     = '$(prefix)'"                >> "constants.py"
	$(INSTALL_DATA) -D constants.py $(DESTDIR)$(pkgdatadir)/interactive-notebook/isystemtap/constants.py

clean-local:
	rm -f constants.py stap-jupyter-container stap-jupyter-install

uninstall-local:
	rm -rf $(DESTDIR)$(pkgdatadir)/interactive-notebook
	rm -f $(DESTDIR)$(bindir)/stap-jupyter-container
	rm -f $(DESTDIR)$(bindir)/stap-jupyter-install
