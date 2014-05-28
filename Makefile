# ------------------------------------
#  synexec - Synchronised Executioner
# ------------------------------------
#  Makefile
# ----------
#  Copyright 2014 (c) Citrix
#
#  This program is free software: you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation, version only.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
# Read the README file for the changelog and information on how to
# compile and use this program.

all:
	make -f Makefile.slave
	make -f Makefile.master

tags:
	ctags -R

clean:
	make -f Makefile.slave clean
	make -f Makefile.master clean

.PHONY: clean tags
