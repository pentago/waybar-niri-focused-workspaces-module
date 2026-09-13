PREFIX ?= /usr
DESTDIR ?=

CARGO ?= cargo
SCDOC ?= scdoc

MODULE := target/release/libniri_focused_workspaces.so
MANPAGE := man/waybar-niri-focused-workspaces.5

.PHONY: all check install uninstall clean

all: $(MODULE) $(MANPAGE)

$(MODULE):
	$(CARGO) build --release

$(MANPAGE): $(MANPAGE).scd
	$(SCDOC) < $< > $@

check:
	$(CARGO) test

install: all
	install -Dm755 $(MODULE) $(DESTDIR)$(PREFIX)/lib/waybar/niri-focused-workspaces.so
	install -Dm644 $(MANPAGE) $(DESTDIR)$(PREFIX)/share/man/man5/waybar-niri-focused-workspaces.5

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/lib/waybar/niri-focused-workspaces.so
	rm -f $(DESTDIR)$(PREFIX)/share/man/man5/waybar-niri-focused-workspaces.5
	-rmdir -p $(DESTDIR)$(PREFIX)/lib/waybar $(DESTDIR)$(PREFIX)/share/man/man5

clean:
	$(CARGO) clean
	rm -f $(MANPAGE)
