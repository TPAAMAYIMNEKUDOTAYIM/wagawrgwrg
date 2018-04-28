# invoke SourceDir generated makefile for empty.pem4f
empty.pem4f: .libraries,empty.pem4f
.libraries,empty.pem4f: package/cfg/empty_pem4f.xdl
	$(MAKE) -f H:\456\labs\lab5/src/makefile.libs

clean::
	$(MAKE) -f H:\456\labs\lab5/src/makefile.libs clean

