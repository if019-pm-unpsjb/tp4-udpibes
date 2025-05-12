SUBDIRS = chat tftp

.PHONY: all clean $(SUBDIRS)

all: $(SUBDIRS)

$(SUBDIRS):
	$(MAKE) -s -C $@

clean:
	@for dir in $(SUBDIRS); do \
		$(MAKE) -s -C $$dir clean; \
	done

zip:
	git archive --format zip --output ${USER}-TP4.zip HEAD
