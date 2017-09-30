# When apps are building, they may refer to some fat headers.
# This file is included by the app build process to pick up these dirs.

ifndef SIGNPOST_USERLAND_BASE_DIR
  $(error SIGNPOST_USERLAND_BASE_DIR not defined)
endif

override CPPFLAGS += -I$(SIGNPOST_USERLAND_BASE_DIR)/support/fatfs/
override CPPFLAGS += -I$(SIGNPOST_USERLAND_BASE_DIR)/support/fatfs/option

