debug ?= no
sanitize ?= no
valgrind ?= no
verbose ?= no
coverage ?= no
time_trace ?= no

libs = rocksdb libzstd libelf libunwind zlib liblz4 snappy bzip2 liburing libxxhash openssl libcurl

# TODO Go over the pkg-config --cflags output for rocksdb
CXX = clang++
CXXFLAGS += -std=c++20 -g -gdwarf-4 -Wall -Wextra -Wno-sign-compare -Wno-unused-parameter -Wno-unused-variable -Wno-gnu-string-literal-operator-template
CPPFLAGS += $(shell pkg-config --cflags $(libs))
LIBS += $(shell pkg-config --libs $(libs)) -ldwarf -lpthread

# Required for XTX's nice backtraces
CXXFLAGS += -fno-pie
LDFLAGS += -no-pie

# use lld to link -- faster
LDFLAGS += -fuse-ld=lld

build_dir := build/

ifeq ($(debug),yes)
    CXXFLAGS += -O0
    CPPFLAGS += -DEGGS_DEBUG
    # FORTIFY_SOURCE doesn't work with -O0. It is added by the rocksdb --cflags
    CPPFLAGS := $(filter-out %FORTIFY_SOURCE=2,$(CPPFLAGS))
    # TODO would be nice to use this, but the headers are not installed by default,
    # something to add when we vendor everything
    # CPPFLAGS += -DROCKSDB_ASSERT_STATUS_CHECKED
    build_dir := $(build_dir)debug
else
    ifeq ($(debug),no)
        CXXFLAGS += -O3
        build_dir := $(build_dir)opt
    else
        $(error debug should be either yes or no)
    endif
endif

ifeq ($(coverage),yes)
    CXXFLAGS += --coverage
    LDFLAGS += --coverage
    build_dir := $(build_dir)-cov
else
    ifneq ($(coverage),no)
        $(error coverage should be either yes or no)
    endif
endif

ifeq ($(sanitize),yes)
    CXXFLAGS += -fsanitize=undefined,address,integer,function -fno-sanitize-recover=null
    LDFLAGS += -fsanitize=undefined,address,integer,function -fno-sanitize-recover=null
    build_dir := $(build_dir)-sanitize
else
    ifneq ($(sanitize),no)
        $(error sanitize should be either yes or no)
    endif
endif

ifeq ($(valgrind),yes)
    ifeq (($sanitize),yes)
        $(error sanitize and valgrind don't play well together)
    endif
    # valgrind chokes on instructions used by -march=native, which is added by
    # the rocksdb --cflags, on our dev servers at least
    CPPFLAGS += -march=haswell
    build_dir := $(build_dir)-valgrind
else
    ifneq ($(valgrind),no)
        $(error valgrind should be either yes or no)
    endif
endif

ifeq ($(time_trace),yes)
    CXXFLAGS += -ftime-trace
    LDFLAGS += -ftime-trace
else
    ifneq ($(time_trace),no)
        $(error time_trace should be either yes or no)
    endif
endif

build_dir := $(build_dir)/

sources := $(sort $(wildcard *.cpp))
objects := $(addprefix $(build_dir), $(sources:.cpp=.o))
binaries_links := $(subst .app.cpp,, $(wildcard *.app.cpp))
binaries := $(addprefix $(build_dir), $(binaries_links))
deps := $(addprefix $(build_dir), $(sources:.cpp=.d))

all: $(binaries_links)

-include $(deps)

%: $(build_dir)%
	ln -sf $^ $@

$(build_dir)%: $(build_dir)%.app.o $(filter-out $(build_dir)%.app.o, $(objects))
	@ echo ld $@
	@ mkdir -p $(build_dir)
	@ $(CXX) $(LDFLAGS) $^ $(LIBS) -o $@

$(build_dir)%.o: %.cpp
	@ echo cc $@
	@ mkdir -p $(build_dir)
	@ $(CXX) $(CPPFLAGS) $(CXXFLAGS) -MD -MP -MF $(addprefix $(build_dir), $(<:.cpp=.d)) -c -o $@ $<

compile_commands.json: $(sources) Makefile
	make debug=yes clean
	bear -- make debug=yes

clean:
	rm -rf $(build_dir) $(binaries_links)

.PHONY: clean all
.NOTINTERMEDIATE: $(objects) $(binaries) 
