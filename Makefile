# Build chan2c99 C library and Python bindings
# Usage:
#   make python    — build _core.so with Python C API integration
#   make test      — build and run the standalone C test program
#   make clean

CC       = gcc
CFLAGS   = -std=c99 -Wall -Wextra -O2 -fPIC -finput-charset=UTF-8
LDFLAGS  = -shared -lm -lpthread

# --- Python binding ---
PYTHON_INCLUDE = $(shell python3 -c "import sysconfig; print(sysconfig.get_path('include'))" 2>/dev/null || echo /usr/include/python3.12)

python: CFLAGS += -I$(PYTHON_INCLUDE)
python: _core.so

_core.so: chan_py.o _core.o
	$(CC) $(LDFLAGS) -o $@ $^

chan_py.o: chan.c chan.h
	$(CC) $(CFLAGS) -c -o $@ $<

_core.o: _core.c chan.h
	$(CC) $(CFLAGS) -c -o $@ $<

# --- Standalone C test ---
chan_test: CFLAGS += -O2
chan_test: LDFLAGS := -lm -lpthread
chan_test: chan.c chan.h
	$(CC) $(CFLAGS) -o $@ chan.c $(LDFLAGS)

test: chan_test
	./chan_test

# --- Multi-threaded test ---
mt-test: CFLAGS += -DCHAN_MULTITHREAD_TEST
mt-test: chan.c chan.h
	$(CC) $(CFLAGS) -O0 -g -o chan_test_mt chan.c -lm -lpthread
	./chan_test_mt --mt-test

# --- Cleanup ---
clean:
	rm -f chan_test chan_test_mt _core.so *.o
