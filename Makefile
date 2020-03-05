help:
	@echo	'make all	build the library and gui'
	@echo	'make lib	build the library'
	@echo	'make gui	build the cyusb gui'
	@echo	'make clean	remove the library'
	@echo	'make install	install everything (must be called as root)'
	@echo	'make uninstall	uninstall everything (must be called as root)'
	@echo	'make deb	build a debian package'

.PHONY: all
all: lib gui

.PHONY: lib
lib:
	g++ -fPIC -o lib/libcyusb.o -c lib/libcyusb.cpp
	g++ -shared -Wl,-soname,libcyusb.so -o lib/libcyusb.so.1 lib/libcyusb.o -l usb-1.0 -l rt
	cd lib; ln -sf libcyusb.so.1 libcyusb.so
	rm -f lib/libcyusb.o

.PHONY: gui
gui:
	cd gui_src && qmake && make

.PHONY: clean
clean:
	rm -f lib/libcyusb.so lib/libcyusb.so.1
	rm -f bin/cyusb
	cd gui_src && make clean

.PHONY: install
install:
	@if [ `whoami` != 'root' ]; then echo "You have to be root to run this script"; exit 1; fi
	rm -f /usr/lib/libcyusb.so* /usr/local/lib/libcyusb.so*
	install -m644 lib/libcyusb.so.1 /usr/local/lib
	cd /usr/local/lib; ln -s libcyusb.so.1 libcyusb.so
	install configs/cy_renumerate.sh /usr/local/bin
	install bin/cyusb /usr/local/bin/
	install -m644 configs/cyusb.conf /etc/
	install -m644 configs/88-cyusb.rules /etc/udev/rules.d/
	echo "# Cypress USB Suite: Root directory" > /etc/profile.d/cyusb
	echo "export CYUSB_ROOT=`pwd`" >> /etc/profile.d/cyusb

.PHONY: uninstall
uninstall:
	@if [ `whoami` != 'root' ]; then echo "You have to be root to run this script"; exit 1; fi
	-rm -f /etc/cyusb.conf
	-rm -f /etc/udev/rules.d/88-cyusb.rules
	-rm -f /usr/lib/libcyusb.so* /usr/local/lib/libcyusb.so*
	-rm -f /usr/local/bin/cyusb
	-rm -f /usr/local/bin/cy_renumerate.sh
	-rm -f /etc/profile.d/cyusb


.PHONY: deb
deb: all
	fakeroot checkinstall --pkgname cyusb --pkgversion 1.0.5 --default --install=no --backup=no --deldoc=yes

