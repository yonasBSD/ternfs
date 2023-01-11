apps := cli/cli integrationtest/integrationtest runeggs/runeggs gcdaemon/gcdaemon

all: $(apps)

$(apps): %:
	@ echo build $@
	@ (cd $(dir $@) && go build .)

clean:
	rm -f $(apps)

.PHONY: all clean $(apps)