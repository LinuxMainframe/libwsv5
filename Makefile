# Makefile for libwsv5 - OBS WebSocket v5 Library
# Builds a static library for OBS WebSocket v5 protocol communication

# Compiler and flags
CC = gcc
AR = ar
CFLAGS = -Wall -Wextra -std=c11 -O2 -fPIC -D_POSIX_C_SOURCE=200809L
INCLUDES = -I. -I/usr/include -I/usr/local/include
LDFLAGS = -L/usr/lib -L/usr/local/lib
LIBS = -lwebsockets -lcjson -lssl -lcrypto -lpthread -lm

# Directories
BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj
LIB_DIR = $(BUILD_DIR)/lib
TEST_DIR = tests
TEST_BIN = $(TEST_DIR)/comprehensive_test

# Source files
LIB_SOURCES = library.c
LIB_OBJECTS = $(OBJ_DIR)/library.o
LIB_TARGET = $(LIB_DIR)/libwsv5.a

TEST_SOURCES = $(TEST_DIR)/comprehensive_test.c
TEST_OBJECTS = $(OBJ_DIR)/comprehensive_test.o

# Default target - builds library and test
all: directories $(LIB_TARGET) $(TEST_BIN)
	@echo ""
	@echo "╔════════════════════════════════════════════════════════════╗"
	@echo "║  libwsv5 - OBS WebSocket v5 Library                       ║"
	@echo "║  Build completed successfully!                            ║"
	@echo "╚════════════════════════════════════════════════════════════╝"
	@echo ""
	@echo "Library:  $(LIB_TARGET)"
	@echo "Test:     $(TEST_BIN)"
	@echo ""
	@echo "Run test with: make test"
	@echo "Or run directly: $(TEST_BIN) --help"
	@echo ""

# Create build directories if they don't exist
directories:
	@mkdir -p $(OBJ_DIR) $(LIB_DIR)

# Build static library from object files
$(LIB_TARGET): $(LIB_OBJECTS)
	@echo "Creating static library: $@"
	$(AR) rcs $@ $^
	@echo "Library created successfully!"

# Build comprehensive test executable
$(TEST_BIN): $(TEST_OBJECTS) $(LIB_TARGET)
	@echo "Building comprehensive test: $@"
	$(CC) $(LDFLAGS) -o $@ $(TEST_OBJECTS) -L$(LIB_DIR) -lwsv5 $(LIBS)
	@echo "Test built successfully!"

# Compile library source to object file
$(OBJ_DIR)/library.o: library.c library.h
	@echo "Compiling: $<"
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Compile test source to object file
$(OBJ_DIR)/comprehensive_test.o: $(TEST_DIR)/comprehensive_test.c library.h
	@echo "Compiling: $<"
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Install library and headers system-wide
install: all
	@echo "Installing libwsv5..."
	install -d $(DESTDIR)/usr/local/lib
	install -d $(DESTDIR)/usr/local/include/libwsv5
	install -m 644 $(LIB_TARGET) $(DESTDIR)/usr/local/lib/
	install -m 644 library.h $(DESTDIR)/usr/local/include/libwsv5/
	@echo "Installation complete!"
	@echo "Library installed to: $(DESTDIR)/usr/local/lib/libwsv5.a"
	@echo "Header installed to: $(DESTDIR)/usr/local/include/libwsv5/library.h"

# Remove installed files
uninstall:
	@echo "Uninstalling libwsv5..."
	rm -f $(DESTDIR)/usr/local/lib/libwsv5.a
	rm -rf $(DESTDIR)/usr/local/include/libwsv5
	@echo "Uninstall complete!"

# Generate API documentation using Doxygen (with automatic PDF generation)
doc:
	@command -v doxygen >/dev/null 2>&1 || { echo "Error: Doxygen not installed"; echo "  Install with: sudo apt-get install doxygen graphviz texlive-latex-base texlive-fonts-recommended"; exit 1; }
	@command -v pdflatex >/dev/null 2>&1 || { echo "Error: pdflatex not installed"; echo "  Install with: sudo apt-get install texlive-latex-base texlive-latex-extra"; exit 1; }
	@echo ""
	@echo "╔════════════════════════════════════════════════════════════╗"
	@echo "║  Generating API Documentation with Doxygen + PDF         ║"
	@echo "╚════════════════════════════════════════════════════════════╝"
	@echo ""
	@echo "Step 1: Generating documentation from source code..."
	@mkdir -p doc
	doxygen Doxyfile
	@echo "  HTML and LaTeX generated"
	@echo ""
	@echo "Step 2: Compiling LaTeX to PDF (this may take a moment)..."
	@cd doc/latex && pdflatex -interaction=nonstopmode -no-shell-escape refman.tex > /dev/null 2>&1 && echo "  First pass complete" || true
	@cd doc/latex && pdflatex -interaction=nonstopmode -no-shell-escape refman.tex > /dev/null 2>&1 && echo "  Second pass complete (cross-references)" || true
	@echo ""
	@echo "Documentation generated successfully!"
	@echo ""
	@if [ -f doc/latex/refman.pdf ]; then \
		SIZE=$$(du -h doc/latex/refman.pdf | cut -f1); \
		echo "Generated files:"; \
		echo "  HTML: doc/html/index.html (with full source browsing)"; \
		echo "  PDF:  doc/latex/refman.pdf ($$SIZE)"; \
	else \
		echo "⚠ Warning: PDF generation may have failed"; \
		echo "  HTML: doc/html/index.html"; \
	fi
	@echo ""
	@echo "View documentation:"
	@echo "  HTML: firefox doc/html/index.html"
	@echo "  PDF:  evince doc/latex/refman.pdf"
	@echo ""

# Clean documentation
doc-clean:
	@echo "Removing generated documentation..."
	rm -rf doc
	@echo "Documentation cleaned!"

# Remove all build artifacts
clean:
	@echo "Cleaning build files..."
	rm -rf $(BUILD_DIR)
	rm -f $(TEST_BIN)
	@echo "Clean complete!"

# Build and run test with default settings (localhost, no password)
test: $(TEST_BIN)
	@echo ""
	@echo "╔════════════════════════════════════════════════════════════╗"
	@echo "║  Running Comprehensive Test Suite                         ║"
	@echo "╚════════════════════════════════════════════════════════════╝"
	@echo ""
	@echo "Using default settings (localhost, no password, debug level 1)"
	@echo "To customize, run: $(TEST_BIN) --help"
	@echo ""
	$(TEST_BIN)

# Run test with custom host and password
test-remote: $(TEST_BIN)
	@echo ""
	@echo "╔════════════════════════════════════════════════════════════╗"
	@echo "║  Running Test with Custom Settings                        ║"
	@echo "╚════════════════════════════════════════════════════════════╝"
	@echo ""
	@echo "Usage: make test-remote HOST=<ip> PASSWORD=<pass> [DEBUG=<0-3>]"
	@echo ""
	@if [ -z "$(HOST)" ]; then \
		echo "Error: HOST not specified"; \
		echo "Example: make test-remote HOST=192.168.1.13 PASSWORD=mypass"; \
		exit 1; \
	fi
	$(TEST_BIN) --host $(HOST) $(if $(PASSWORD),--password $(PASSWORD)) $(if $(DEBUG),--debug $(DEBUG))

# Run test with maximum debug verbosity
test-verbose: $(TEST_BIN)
	@echo ""
	@echo "╔════════════════════════════════════════════════════════════╗"
	@echo "║  Running Test with Verbose Debug Output (Level 3)         ║"
	@echo "╚════════════════════════════════════════════════════════════╝"
	@echo ""
	$(TEST_BIN) --debug 3

# Valgrind memory leak detection
valgrind: $(TEST_BIN)
	@echo ""
	@echo "╔════════════════════════════════════════════════════════════╗"
	@echo "║  Running Valgrind Memory Leak Detection                   ║"
	@echo "╚════════════════════════════════════════════════════════════╝"
	@echo ""
	@echo "Usage: make valgrind [HOST=<ip>] [PASSWORD=<pass>] [DEBUG=<0-3>]"
	@echo ""
	valgrind --leak-check=full \
		--show-leak-kinds=all \
		--track-origins=yes \
		--verbose \
		--log-file=valgrind-out.txt \
		$(TEST_BIN) $(if $(HOST),--host $(HOST),--host localhost) \
		$(if $(PASSWORD),--password $(PASSWORD)) \
		$(if $(DEBUG),--debug $(DEBUG),--debug 1)
	@echo ""
	@echo "Valgrind analysis complete!"
	@echo "Full report saved to: valgrind-out.txt"
	@echo ""
	@echo "Summary:"
	@grep -E "(ERROR SUMMARY|definitely lost|indirectly lost|possibly lost|still reachable)" valgrind-out.txt || true
	@echo ""

# Valgrind with suppression of known library issues
valgrind-clean: $(TEST_BIN)
	@echo ""
	@echo "╔════════════════════════════════════════════════════════════╗"
	@echo "║  Running Valgrind (Suppressing Library Issues)             ║"
	@echo "╚════════════════════════════════════════════════════════════╝"
	@echo ""
	valgrind --leak-check=full \
		--show-leak-kinds=definite,possible \
		--track-origins=yes \
		--suppressions=/usr/share/gdb/auto-load/usr/lib/x86_64-linux-gnu/libstdc++.so.6.0.30-gdb.py 2>/dev/null || true \
		--log-file=valgrind-clean.txt \
		$(TEST_BIN) $(if $(HOST),--host $(HOST),--host localhost) \
		$(if $(PASSWORD),--password $(PASSWORD)) \
		$(if $(DEBUG),--debug $(DEBUG),--debug 0)
	@echo ""
	@echo "Valgrind analysis complete!"
	@echo "Report saved to: valgrind-clean.txt"
	@echo ""

# Valgrind memory error detection (faster, no leak check)
valgrind-memcheck: $(TEST_BIN)
	@echo ""
	@echo "╔════════════════════════════════════════════════════════════╗"
	@echo "║  Running Valgrind Memory Error Detection                  ║"
	@echo "╚════════════════════════════════════════════════════════════╝"
	@echo ""
	valgrind --tool=memcheck \
		--track-origins=yes \
		--read-var-info=yes \
		--log-file=valgrind-memcheck.txt \
		$(TEST_BIN) $(if $(HOST),--host $(HOST),--host localhost) \
		$(if $(PASSWORD),--password $(PASSWORD)) \
		$(if $(DEBUG),--debug $(DEBUG),--debug 0)
	@echo ""
	@echo "Memory error check complete!"
	@echo "Report saved to: valgrind-memcheck.txt"
	@echo ""

# Valgrind thread error detection (helgrind)
valgrind-thread: $(TEST_BIN)
	@echo ""
	@echo "╔════════════════════════════════════════════════════════════╗"
	@echo "║  Running Valgrind Thread Error Detection (Helgrind)       ║"
	@echo "╚════════════════════════════════════════════════════════════╝"
	@echo ""
	valgrind --tool=helgrind \
		--log-file=valgrind-helgrind.txt \
		$(TEST_BIN) $(if $(HOST),--host $(HOST),--host localhost) \
		$(if $(PASSWORD),--password $(PASSWORD)) \
		$(if $(DEBUG),--debug $(DEBUG),--debug 0)
	@echo ""
	@echo "Thread analysis complete!"
	@echo "Report saved to: valgrind-helgrind.txt"
	@echo ""

# Valgrind data race detection (drd)
valgrind-race: $(TEST_BIN)
	@echo ""
	@echo "╔════════════════════════════════════════════════════════════╗"
	@echo "║  Running Valgrind Data Race Detection (DRD)               ║"
	@echo "╚════════════════════════════════════════════════════════════╝"
	@echo ""
	valgrind --tool=drd \
		--log-file=valgrind-drd.txt \
		$(TEST_BIN) $(if $(HOST),--host $(HOST),--host localhost) \
		$(if $(PASSWORD),--password $(PASSWORD)) \
		$(if $(DEBUG),--debug $(DEBUG),--debug 0)
	@echo ""
	@echo "Data race analysis complete!"
	@echo "Report saved to: valgrind-drd.txt"
	@echo ""

# Run all valgrind tests
valgrind-all: valgrind valgrind-memcheck valgrind-thread valgrind-race
	@echo ""
	@echo "╔════════════════════════════════════════════════════════════╗"
	@echo "║  All Valgrind Tests Complete                               ║"
	@echo "╚════════════════════════════════════════════════════════════╝"
	@echo ""
	@echo "Reports generated:"
	@echo "  - valgrind-out.txt       (Memory leaks)"
	@echo "  - valgrind-memcheck.txt  (Memory errors)"
	@echo "  - valgrind-helgrind.txt  (Thread errors)"
	@echo "  - valgrind-drd.txt       (Data races)"
	@echo ""

# Help target
help:
	@echo "╔════════════════════════════════════════════════════════════╗"
	@echo "║  libwsv5 Makefile - Available Targets                     ║"
	@echo "╚════════════════════════════════════════════════════════════╝"
	@echo ""
	@echo "Build Targets:"
	@echo "  all              Build library and test (default)"
	@echo "  clean            Remove all build files"
	@echo "  install          Install library and headers system-wide"
	@echo "  uninstall        Remove installed files"
	@echo ""
	@echo "Documentation Targets:"
	@echo "  doc              Generate HTML and PDF documentation with Doxygen"
	@echo "  doc-clean        Remove generated documentation"
	@echo ""
	@echo "Test Targets:"
	@echo "  test             Build and run test with default settings"
	@echo "  test-remote      Run test with custom host/password"
	@echo "  test-verbose     Run test with verbose debug output (level 3)"
	@echo ""
	@echo "Valgrind Targets:"
	@echo "  valgrind         Memory leak detection (comprehensive)"
	@echo "  valgrind-clean   Memory leak detection (suppress library issues)"
	@echo "  valgrind-memcheck Memory error detection"
	@echo "  valgrind-thread  Thread error detection (helgrind)"
	@echo "  valgrind-race    Data race detection (drd)"
	@echo "  valgrind-all     Run all valgrind tests"
	@echo ""
	@echo "Build Outputs:"
	@echo "  Library:         $(LIB_TARGET)"
	@echo "  Test:            $(TEST_BIN)"
	@echo "  Documentation:   doc/html/index.html, doc/latex/refman.pdf"
	@echo ""
	@echo "Usage Examples:"
	@echo "  make                                    # Build everything"
	@echo "  make clean                              # Clean build files"
	@echo "  make doc                                # Generate documentation"
	@echo "  make test                               # Run test (localhost)"
	@echo "  make test-remote HOST=192.168.1.13 PASSWORD=mypass"
	@echo "  make test-remote HOST=10.0.0.5 PASSWORD=secret DEBUG=3"
	@echo "  make test-verbose                       # Run with debug level 3"
	@echo "  sudo make install                       # Install system-wide"
	@echo ""
	@echo "Direct Test Execution:"
	@echo "  $(TEST_BIN) --help"
	@echo "  $(TEST_BIN) --host 192.168.1.13 --password mypass"
	@echo "  $(TEST_BIN) -h localhost -p 4455 -w secret -d 3"
	@echo ""

# Print configuration
info:
	@echo "╔════════════════════════════════════════════════════════════╗"
	@echo "║  Build Configuration                                       ║"
	@echo "╚════════════════════════════════════════════════════════════╝"
	@echo ""
	@echo "Compiler:"
	@echo "  CC:              $(CC)"
	@echo "  CFLAGS:          $(CFLAGS)"
	@echo "  INCLUDES:        $(INCLUDES)"
	@echo "  LDFLAGS:         $(LDFLAGS)"
	@echo "  LIBS:            $(LIBS)"
	@echo ""
	@echo "Directories:"
	@echo "  BUILD_DIR:       $(BUILD_DIR)"
	@echo "  OBJ_DIR:         $(OBJ_DIR)"
	@echo "  LIB_DIR:         $(LIB_DIR)"
	@echo "  TEST_DIR:        $(TEST_DIR)"
	@echo ""
	@echo "Targets:"
	@echo "  Library:         $(LIB_TARGET)"
	@echo "  Test:            $(TEST_BIN)"
	@echo ""

# Phony targets - these don't represent actual files
.PHONY: all directories clean install uninstall doc doc-clean test test-remote test-verbose \
        valgrind valgrind-clean valgrind-memcheck valgrind-thread valgrind-race valgrind-all \
        help info