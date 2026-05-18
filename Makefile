# Build chan2c99 shared library for Python bindings
# Usage:
#   make           — build libchan.so (standalone, no Python C API)
#   make python    — build _core.so with Python C API integration
#   make test      — build and run the standalone C test program
#   make clean

CC       = gcc
CFLAGS   = -std=c99 -Wall -Wextra -O2 -fPIC -finput-charset=UTF-8
LDFLAGS  = -shared -lm -lpthread

# --- Python binding ---
# Adjust PYTHON_INCLUDE to match your Python version:
#   python3 -c "import sysconfig; print(sysconfig.get_path('include'))"
PYTHON_INCLUDE = $(shell python3 -c "import sysconfig; print(sysconfig.get_path('include'))" 2>/dev/null || echo /usr/include/python3.12)

libchan.so: chan.o chan_wrapper.o
	$(CC) $(LDFLAGS) -o $@ $^

chan.o: chan.c chan.h
	$(CC) $(CFLAGS) -c -o $@ $<

chan_wrapper.o: chan_wrapper.c chan_wrapper.h chan.h
	$(CC) $(CFLAGS) -c -o $@ $<

python: CFLAGS += -DPYTHON_WRAPPER -I$(PYTHON_INCLUDE)
python: _core.so

_core.so: chan_py.o chan_wrapper_py.o
	$(CC) $(LDFLAGS) -o $@ $^

chan_py.o: chan.c chan.h
	$(CC) $(CFLAGS) -c -o $@ $<

chan_wrapper_py.o: chan_wrapper.c chan_wrapper.h chan.h
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
	rm -f chan_test chan_test_mt libchan.so _core.so *.o
