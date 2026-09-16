CC = clang
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -O2 -Isrc
NCURSES = -lncurses
COCOA = -framework Cocoa -framework UniformTypeIdentifiers -framework IOKit -framework CoreText

SRC = src/doc.c src/wpd.c src/view.c src/cmd.c src/files.c src/screen.c src/iofmt.c src/font.c src/agent.c
HDR = src/wp51.h src/doc.h src/wpd.h src/view.h src/cmd.h src/files.h src/screen.h src/gui.h src/iofmt.h src/font.h src/agent.h

.PHONY: all test clean app install

all: wp

src/gui.o: src/gui.m src/gui.h $(HDR)
	$(CC) -fobjc-arc -Wall -O2 -Isrc -c src/gui.m -o src/gui.o

wp: src/main.c src/gui.o $(SRC) $(HDR)
	$(CC) $(CFLAGS) -o wp src/main.c $(SRC) src/gui.o $(NCURSES) $(COCOA)

app: wp
	mkdir -p WordPerfect.app/Contents/MacOS WordPerfect.app/Contents/Resources
	cp wp WordPerfect.app/Contents/MacOS/WordPerfect
	cp macos/Info.plist WordPerfect.app/Contents/Info.plist
	cp macos/AppIcon.icns WordPerfect.app/Contents/Resources/AppIcon.icns

LSREGISTER = /System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister

install: app
	ditto WordPerfect.app /Applications/WordPerfect.app
	$(LSREGISTER) -f /Applications/WordPerfect.app

tests/test_core: tests/test_core.c src/doc.c src/wpd.c src/iofmt.c src/view.c src/screen.c src/font.c src/doc.h src/wpd.h src/wp51.h src/iofmt.h src/view.h src/font.h
	$(CC) $(CFLAGS) -o tests/test_core tests/test_core.c src/doc.c src/wpd.c src/iofmt.c src/view.c src/screen.c src/font.c $(NCURSES)

tests/test_corpus: tests/test_corpus.c src/doc.c src/wpd.c src/iofmt.c src/view.c src/screen.c src/font.c src/doc.h src/wpd.h src/wp51.h src/iofmt.h src/view.h src/font.h
	$(CC) $(CFLAGS) -o tests/test_corpus tests/test_corpus.c src/doc.c src/wpd.c src/iofmt.c src/view.c src/screen.c src/font.c $(NCURSES)

test: tests/test_core tests/test_corpus wp
	./tests/test_core
	./tests/test_corpus
	mkdir -p tests/out
	rm -f tests/out/ui.wpd
	./wp --script tests/flow.script
	test -f tests/out/ui.wpd
	python3 tests/check_ui_wpd.py
	./wp --script tests/load.script tests/out/ui.wpd
	./wp --script tests/quit.script
	./wp --script tests/ctrlc.script
	./wp --script tests/keys.script
	test -f tests/out/keys.wpd
	python3 tests/check_keys_wpd.py
	python3 tests/tui_smoke.py
	./wp --script tests/font.script
	test -f tests/out/font.wpd
	python3 tests/check_font_wpd.py
	./wp --script tests/size.script
	test -f tests/out/size.wpd
	python3 tests/check_size.py
	./wp --script tests/just.script
	test -f tests/out/just.wpd
	python3 tests/check_just.py
	python3 tests/test_declaration_size.py
	python3 tests/test_agent.py
	./wp --help 2>&1 | grep -q -- --gui
	./wp --help 2>&1 | grep -q -- --agent

clean:
	rm -f wp tests/test_core tests/test_corpus src/gui.o
	rm -rf tests/out WordPerfect.app
