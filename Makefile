# Frontend to dune.

.PHONY: default build install uninstall test clean

default: build

build:
	dune build

test:
	dune runtest -f

cover:
	BISECT_ENABLE=yes dune runtest -f && bisect-ppx-report -I _build/default/ --html _coverage/ `find . -name 'bisect*.out'`

install:
	dune install

uninstall:
	dune uninstall

clean:
	dune clean
# Optionally, remove all files/folders ignored by git as defined
# in .gitignore (-X).
	git clean -dfXq
