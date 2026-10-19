## Process this file with automake to create Makefile.in.

SUBDIRS = ext.d macros.d skins syntax

LIBFILES_OUT = mc.ext.ini

noinst_DATA = xterm.ad

PKGDATA_IN = mc.charsets.in

PKGDATA_OUT = mc.charsets

dist_pkgdata_DATA = \
	mc.lib

pkgdata_DATA = \
	$(PKGDATA_OUT)

SCRIPTS_IN = \
	mc.menu.in \
	mcedit.menu.in

SCRIPTS_OUT = \
	mc.menu \
	mcedit.menu

LIBFILES_CONST = \
	filehighlight.ini \
	exclude \
	mc.default.keymap \
	mc.emacs.keymap \
	mc.vim.keymap


LIBFILES_SCRIPT = \
	edit.indent.rc

CLEANFILES = $(SCRIPTS_OUT) $(PKGDATA_OUT)

cfgdir = $(sysconfdir)/@PACKAGE@
cfg_DATA = \
	$(LIBFILES_CONST) \
	$(SCRIPTS_OUT) \
	$(LIBFILES_OUT)

cfg_SCRIPTS = \
	$(LIBFILES_SCRIPT)

# Files processed by configure don't need to be here
EXTRA_DIST = \
	$(LIBFILES_CONST) \
	$(LIBFILES_SCRIPT) \
	$(SCRIPTS_IN) \
	$(noinst_DATA) \
	$(PKGDATA_IN)

install-data-hook:
	cd $(DESTDIR)$(cfgdir) && rm -f mc.keymap && $(LN_S) mc.default.keymap mc.keymap

uninstall-hook:
	cd $(DESTDIR)$(cfgdir) && rm -f mc.keymap

# vivid file types and color themes (see misc/vivid/README.md)
dist_pkgdata_DATA += \
	vivid/filetypes.yml

vividthemesdir = $(pkgdatadir)/themes
dist_vividthemes_DATA = \
	vivid/themes/alabaster_dark.yml \
	vivid/themes/alucard.yml \
	vivid/themes/ansi.yml \
	vivid/themes/ayu.yml \
	vivid/themes/catppuccin-frappe.yml \
	vivid/themes/catppuccin-latte.yml \
	vivid/themes/catppuccin-macchiato.yml \
	vivid/themes/catppuccin-mocha.yml \
	vivid/themes/cyberdream-light.yml \
	vivid/themes/cyberdream.yml \
	vivid/themes/dracula.yml \
	vivid/themes/gruvbox-dark-hard.yml \
	vivid/themes/gruvbox-dark-soft.yml \
	vivid/themes/gruvbox-dark.yml \
	vivid/themes/gruvbox-light-hard.yml \
	vivid/themes/gruvbox-light-soft.yml \
	vivid/themes/gruvbox-light.yml \
	vivid/themes/iceberg-dark.yml \
	vivid/themes/jellybeans.yml \
	vivid/themes/lava.yml \
	vivid/themes/modus-operandi.yml \
	vivid/themes/modus-vivendi.yml \
	vivid/themes/molokai.yml \
	vivid/themes/nord.yml \
	vivid/themes/one-dark.yml \
	vivid/themes/one-light.yml \
	vivid/themes/rose-pine-dawn.yml \
	vivid/themes/rose-pine-moon.yml \
	vivid/themes/rose-pine.yml \
	vivid/themes/snazzy.yml \
	vivid/themes/solarized-dark.yml \
	vivid/themes/solarized-light.yml \
	vivid/themes/tokyonight-day.yml \
	vivid/themes/tokyonight-moon.yml \
	vivid/themes/tokyonight-night.yml \
	vivid/themes/tokyonight-storm.yml \
	vivid/themes/zenburn.yml

EXTRA_DIST += \
	vivid/README.md \
	vivid/LICENSE-MIT \
	vivid/LICENSE-APACHE
