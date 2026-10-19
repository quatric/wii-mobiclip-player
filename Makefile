#---------------------------------------------------------------------------------
# Clear the implicit built in rules
#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------
ifeq ($(strip $(DEVKITPPC)),)
$(error "Please set DEVKITPPC in your environment. export DEVKITPPC=<path to>devkitPPC")
endif

include $(DEVKITPPC)/wii_rules

#---------------------------------------------------------------------------------
TARGET		:=	wii-mobiclip-player
BUILD		:=	build
SOURCES		:=	source libwiiinput/source
DATA		:=	data
INCLUDES	:=	source

#---------------------------------------------------------------------------------
CFLAGS		=	-g -O3 -Wall $(MACHDEP) $(INCLUDE)
# Version for the update check: the release tag when CI builds one, else meta.xml
APP_VERSION	?=	$(if $(filter refs/tags/%,$(GITHUB_REF)),$(GITHUB_REF_NAME),$(shell sed -n 's:.*<version>\(.*\)</version>.*:\1:p' $(dir $(abspath $(firstword $(MAKEFILE_LIST))))meta.xml))
CFLAGS	+=	-DAPP_VERSION=\"$(APP_VERSION)\"
CXXFLAGS	=	$(CFLAGS)
LDFLAGS		=	-g $(MACHDEP) -Wl,-Map,$(notdir $@).map

LIBS		:=	-lfat -lwiikeyboard -lwiiuse -lbte -lasnd -lvorbisidec -logg -logc -lm
LIBS	:=	-lwiiupdate $(LIBS)

# Optional controller backends. Install each library in portlibs/ppc.
WITH_WIIDRC	?=	0
WITH_WUPC	?=	0
WITH_SICKSAXIS	?=	0

ifeq ($(WITH_WIIDRC),1)
	CFLAGS		+=	-DHAVE_WIIDRC
	LIBS		:=	-lwiidrc $(LIBS)
endif
ifeq ($(WITH_WUPC),1)
	CFLAGS		+=	-DHAVE_WUPC
	LDFLAGS		+=	-Wl,-wrap,wiiuse_accept
	LIBS		:=	-lwupc $(LIBS)
endif
ifeq ($(WITH_SICKSAXIS),1)
	CFLAGS		+=	-DHAVE_SICKSAXIS
	LIBS		:=	-lsicksaxis $(LIBS)
endif

LIBDIRS		:=	$(CURDIR)/portlibs/ppc $(CURDIR)/libwiiinput
LIBDIRS	+=	$(CURDIR)/libwiiupdate

#---------------------------------------------------------------------------------
# The .dol that goes into the Homebrew Channel package.
ifeq ($(NO_DOLXZ),1)
BOOT_DOL	=	$(TARGET).dol
else
BOOT_DOL	=	$(TARGET)-xz.dol
endif

ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------
export OUTPUT	:=	$(CURDIR)/$(TARGET)
export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
					$(foreach dir,$(DATA),$(CURDIR)/$(dir))
export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
sFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.S)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

ifeq ($(strip $(CPPFILES)),)
	export LD	:=	$(CC)
else
	export LD	:=	$(CXX)
endif

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES))
export OFILES_SOURCES :=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) \
							$(sFILES:.s=.o) $(SFILES:.S=.o)
export OFILES	:=	$(OFILES_BIN) $(OFILES_SOURCES)
export HFILES	:=	$(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
					$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
					-I$(CURDIR)/$(BUILD) \
					-I$(LIBOGC_INC)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib) \
					-L$(LIBOGC_LIB)

export OUTPUT	:=	$(CURDIR)/$(TARGET)
.PHONY: $(BUILD) clean

#---------------------------------------------------------------------------------
$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

$(BUILD): libwiiupdate-lib
.PHONY: libwiiupdate-lib
libwiiupdate-lib:
	@$(MAKE) --no-print-directory -C $(CURDIR)/libwiiupdate

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr $(BUILD) $(OUTPUT).elf $(OUTPUT).dol $(OUTPUT)-xz.dol

#---------------------------------------------------------------------------------
run:
	wiiload $(OUTPUT).dol

#---------------------------------------------------------------------------------
else

DEPENDS	:=	$(OFILES:.o=.d)

#---------------------------------------------------------------------------------
#---------------------------------------------------------------------------------
# Self-extracting xz-compressed copy of the .dol (FIX94's dolxz, see
# scripts/dolxz.sh). It is the default target; NO_DOLXZ=1 skips it.
#---------------------------------------------------------------------------------
ifneq ($(NO_DOLXZ),1)
$(OUTPUT)-xz.dol: $(OUTPUT).dol
	@echo compressing ... $(notdir $@)
	@sh $(dir $(OUTPUT))scripts/dolxz.sh $< $@
endif

$(OUTPUT).dol: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)

$(OFILES_SOURCES) : $(HFILES)

-include $(DEPENDS)

#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------
