##
#
# Compiz plugin Makefile
#
# Copyright : (C) 2006 by Dennis Kasprzyk
# E-mail    : dennis.kasprzyk@rwth-aachen.de
#
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
##

DESTDIR = $(HOME)/.compiz/plugins
PLUGIN = animation
SHADER = yes

BUILDDIR = build

CC        = gcc
LIBTOOL   = libtool
INSTALL   = install

CGC       = /usr/bin/cgc

#CFLAGS  = -g -Wall `pkg-config --cflags x11 compiz`
CFLAGS  = -g -Wall -DGL_DEBUG `pkg-config --cflags x11 compiz`
LDFLAGS = `pkg-config --libs x11 `

vert-shaders := $(patsubst %.vcg,%.vert,$(shell find -name '*.vcg' 2> /dev/null | sed -e 's/^.\///'))
frag-shaders := $(patsubst %.fcg,%.frag,$(shell find -name '*.fcg' 2> /dev/null | sed -e 's/^.\///'))

shader_files := $(shell find -name '*.vcg' 2> /dev/null | sed -e 's/^.\///')
shader_files += $(shell find -name '*.fcg' 2> /dev/null | sed -e 's/^.\///')

headers := $(shell find -name '$(PLUGIN)*.h' 2> /dev/null | sed -e 's/^.\///')
headers := $(filter-out $(PLUGIN)_shader.h,$(headers))

shader_header := $(shell if [ -x $(CGC) -a '$(SHADER)' == 'yes' ]; then echo "$(PLUGIN)_shader.h"; fi )


shaders := $(addprefix $(BUILDDIR)/,$(vert-shaders))
shaders += $(addprefix $(BUILDDIR)/,$(frag-shaders))

color := $(shell if [ $$TERM = "dumb" ]; then echo "no"; else echo "yes"; fi)

.PHONY: $(BUILDDIR)/compiz_$(PLUGIN).csm

all: $(BUILDDIR) $(shader_header) $(BUILDDIR)/lib$(PLUGIN).la

$(BUILDDIR) :
	@mkdir $(BUILDDIR)

$(BUILDDIR)/lib$(PLUGIN).lo: $(PLUGIN).c $(headers) $(shader_header)
	@if [ '$(color)' != 'no' ]; then \
	    echo -n -e "\033[0;1;5mcompiling \033[0;1;37m: \033[0;32m$< \033[0;1;37m-> \033[0;31m$@\033[0m"; \
	else \
	    echo "compiling : $< -> $@"; \
	fi
	@$(LIBTOOL) --quiet --mode=compile $(CC) $(CFLAGS) -c -o $@ $<
	@if [ '$(color)' != 'no' ]; then \
	    echo -e "\r\033[0mcompiling : \033[34m$< -> $@\033[0m"; \
	fi

$(BUILDDIR)/lib$(PLUGIN).la: $(BUILDDIR)/lib$(PLUGIN).lo
	@if [ '$(color)' != 'no' ]; then \
	    echo -n -e "\033[0;1;5mlinking   \033[0;1;37m: \033[0;32m$< \033[0;1;37m-> \033[0;31m$@\033[0m"; \
	else \
	    echo "linking   : $< -> $@"; \
	fi
	@$(LIBTOOL) --quiet --mode=link $(CC) $(LDFLAGS) -rpath $(DESTDIR) -o $@ $<
	@if [ '$(color)' != 'no' ]; then \
	    echo -e "\r\033[0mlinking   : \033[34m$< -> $@\033[0m"; \
	fi

install: all
	@if [ '$(color)' != 'no' ]; then \
	    echo -n -e "\033[0;1;5minstall   \033[0;1;37m: \033[0;31m$(DESTDIR)/lib$(PLUGIN).so\033[0m"; \
	else \
	    echo "install   : $(DESTDIR)/lib$(PLUGIN).so"; \
	fi
	@mkdir -p $(DESTDIR)
	@$(INSTALL) $(BUILDDIR)/.libs/lib$(PLUGIN).so $(DESTDIR)/lib$(PLUGIN).so
	@if [ '$(color)' != 'no' ]; then \
	    echo -e "\r\033[0minstall   : \033[34m$(DESTDIR)/lib$(PLUGIN).so\033[0m"; \
	fi

clean:
	rm -rf $(BUILDDIR)

csm: $(BUILDDIR) $(BUILDDIR)/compiz_$(PLUGIN).csm


csm-install: csm
	@if [ '$(color)' != 'no' ]; then \
	    echo -n -e "\033[0;1;5minstall   \033[0;1;37m: \033[0;31m$(DESTDIR)/compiz_$(PLUGIN).csm\033[0m"; \
	else \
	    echo "install   : $(DESTDIR)/lib$(PLUGIN).so"; \
	fi
	@mkdir -p $(DESTDIR)
	@$(INSTALL) $(BUILDDIR)/compiz_$(PLUGIN).csm $(DESTDIR)/compiz_$(PLUGIN).csm
	@if [ '$(color)' != 'no' ]; then \
	    echo -e "\r\033[0minstall   : \033[34m$(DESTDIR)/compiz_$(PLUGIN).csm\033[0m"; \
	fi

$(BUILDDIR)/compiz_$(PLUGIN).csm:
	@if [ '$(color)' != 'no' ]; then \
	    echo -n -e "\033[0;1;5mcreating  \033[0;1;37m: \033[0;31m$(DESTDIR)/compiz_$(PLUGIN).csm\033[0m"; \
	else \
	    echo "creating  : $(DESTDIR)/compiz_$(PLUGIN).csm"; \
	fi
	@COMPIZ_SCHEMA_PLUGINS="$(PLUGIN)" COMPIZ_SCHEMA_FILE="$(BUILDDIR)/compiz_$(PLUGIN).csm" compiz --replace $(PLUGIN) csm-dump &> /dev/null
	@if [ '$(color)' != 'no' ]; then \
	    echo -e "\r\033[0mcreating  : \033[34m$(DESTDIR)/compiz_$(PLUGIN).csm\033[0m"; \
	fi

$(BUILDDIR)/%.vert: %.vcg
	@if [ '$(color)' != 'no' ]; then \
	    echo -n -e "\033[0;1;5mcompiling \033[0;1;37m: \033[0;32m$< \033[0;1;37m-> \033[0;31m$@\033[0m"; \
	else \
	    echo "compiling : $< -> $@"; \
	fi
	@$(CGC) -unroll all -quiet -fastmath -fastprecision -profile arbvp1 -o $@ $<
	@if [ '$(color)' != 'no' ]; then \
	    echo -e "\r\033[0mcompiling : \033[34m$< -> $@\033[0m"; \
	fi

$(BUILDDIR)/%.frag: %.fcg
	@if [ '$(color)' != 'no' ]; then \
	    echo -n -e "\033[0;1;5mcompiling \033[0;1;37m: \033[0;32m$< \033[0;1;37m-> \033[0;31m$@\033[0m"; \
	else \
	    echo "compiling : $< -> $@"; \
	fi
	@$(CGC) -unroll all -quiet -fastmath -fastprecision -profile arbfp1 -o $@ $<
	@if [ '$(color)' != 'no' ]; then \
	    echo -e "\r\033[0mcompiling : \033[34m$< -> $@\033[0m"; \
	fi


$(PLUGIN)_shader.h: $(shaders)
	@if [ '$(color)' != 'no' ]; then \
	    echo -n -e "\033[0;1;5mcreating  \033[0;1;37m: \033[0;31m$@\033[0m"; \
	else \
	    echo "creating  : $@"; \
	fi
	@echo "/**" > $(BUILDDIR)/$(PLUGIN)_shader.h.tmp
	@echo " *" >> $(BUILDDIR)/$(PLUGIN)_shader.h.tmp
	@echo " * This file is autogenerated from :" >> $(BUILDDIR)/$(PLUGIN)_shader.h.tmp
	@echo " * $(shader_files)" >> $(BUILDDIR)/$(PLUGIN)_shader.h.tmp
	@echo " *" >> $(BUILDDIR)/$(PLUGIN)_shader.h.tmp
	@echo " * This program is distributed in the hope that it will be useful," >> $(BUILDDIR)/$(PLUGIN)_shader.h.tmp
	@echo " * but WITHOUT ANY WARRANTY; without even the implied warranty of" >> $(BUILDDIR)/$(PLUGIN)_shader.h.tmp
	@echo " * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the" >> $(BUILDDIR)/$(PLUGIN)_shader.h.tmp
	@echo " * GNU General Public License for more details." >> $(BUILDDIR)/$(PLUGIN)_shader.h.tmp
	@echo " *" >> $(BUILDDIR)/$(PLUGIN)_shader.h.tmp
	@echo " **/" >> $(BUILDDIR)/$(PLUGIN)_shader.h.tmp
	@echo "#ifndef $(PLUGIN)_SHADER_H" >> $(BUILDDIR)/$(PLUGIN)_shader.h.tmp
	@echo "#define $(PLUGIN)_SHADER_H" >> $(BUILDDIR)/$(PLUGIN)_shader.h.tmp
	@for i in $(shaders); do \
	    echo >> $(BUILDDIR)/$(PLUGIN)_shader.h.tmp; \
	    echo "static const char* `basename $$i | sed -s 's/\..*$$//'` = ">> $(BUILDDIR)/$(PLUGIN)_shader.h.tmp; \
	    cat $$i | grep -v "^#" | sed -e 's/^/\t"/' | sed -e 's/$$/\\n"/' >> $(BUILDDIR)/$(PLUGIN)_shader.h.tmp; \
	    echo ";" >> $(BUILDDIR)/$(PLUGIN)_shader.h.tmp; \
	done
	@echo >> $(BUILDDIR)/$(PLUGIN)_shader.h.tmp
	@echo "#endif" >> $(BUILDDIR)/$(PLUGIN)_shader.h.tmp
	@cp $(BUILDDIR)/$(PLUGIN)_shader.h.tmp $(PLUGIN)_shader.h
	@if [ '$(color)' != 'no' ]; then \
	    echo -e "\r\033[0mcreating  : \033[34m$@\033[0m"; \
	fi
