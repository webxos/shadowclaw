# Makefile for Shadowclaw

CC = gcc
CFLAGS = -Wall -Wextra -O2 -D_GNU_SOURCE
LDFLAGS = -lpthread -lcurl -lm

# Dependency checks
CHECK_CURL = $(shell pkg-config --exists libcurl && echo yes)
ifeq ($(CHECK_CURL),)
$(error libcurl development package not found (install libcurl4-openssl-dev or equivalent))
endif

# Source files
OBJS = shadowclaw.o interpreter.o cJSON.o

# Default target
shadowclaw: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

# Automatic header dependency tracking
DEPDIR = .deps
df = $(DEPDIR)/$(*F)

%.o: %.c
	@mkdir -p $(DEPDIR)
	$(CC) $(CFLAGS) -MMD -MF $(df).d -c $< -o $@
	@cp $(df).d $(df).P; sed -e 's/#.*//' -e 's/^[^:]*: *//' -e 's/ *\\$$//' -e '/^$$/ d' -e 's/$$/ :/' < $(df).d >> $(df).P; rm -f $(df).d

# Include generated dependencies
-include $(OBJS:%.o=$(DEPDIR)/%.P)

# Clean
clean:
	rm -f shadowclaw $(OBJS) $(DEPDIR)/*.P

.PHONY: clean
