# PREFIX is environment variable, but if it is not set, then set default value
ifeq ($(PREFIX),)
    PREFIX := /usr/local
endif

# VRT trace tools
all: astropeiler_trace astropeiler_trace_external

#INCLUDES = -I.
#LIBS = -L.

CFLAGS = -std=c++11
INCLUDES = -I. -I/opt/local/include -I../libvrt/include -I/opt/homebrew/include/
LIBS = -L. -L../libvrt/build/ -L/usr/local/lib -L/opt/local/lib -L/opt/homebrew/lib/

BOOSTLIBS = -lboost_system -lboost_program_options -lboost_chrono -lboost_filesystem -lboost_thread -lboost_date_time
# BOOSTLIBS = -lboost_system-mt -lboost_program_options-mt -lboost_chrono-mt -lboost_filesystem-mt -lboost_thread-mt -lboost_date_time-mt


astropeiler_trace_external: astropeiler_trace_external.cpp
		g++ -O3 $(INCLUDES) $(LIBS) $(CFLAGS) astropeiler_trace_external.cpp -o astropeiler_trace_external \
		$(BOOSTLIBS) -lzmq -lvrt -lcurl

astropeiler_trace: astropeiler_trace.cpp
		g++ -O3 $(INCLUDES) $(LIBS) $(CFLAGS) astropeiler_trace.cpp -o astropeiler_trace \
		$(BOOSTLIBS) -lzmq -lvrt -lcurl

clean:
		$(RM) astropeiler_trace
