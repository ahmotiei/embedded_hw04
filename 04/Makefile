.PHONY: all clean test

all:
	$(MAKE) -C snmp

clean:
	$(MAKE) -C snmp clean
	rm -rf generated/* test-output/*

test: all
	./scripts/test_pass_handler.sh config/master.env
