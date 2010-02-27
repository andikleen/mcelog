.PHONY: test clean

DEBUG =

test:
	./test cache "${DEBUG}"
	./test page "${DEBUG}"
	./test memdb "${DEBUG}"
	./test socket "${DEBUG}"

clean:
	rm -f */*log
	rm -f */results
