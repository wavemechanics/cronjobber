.PHONY: help

# self-documenting Makefile thanks to
# https://marmelab.com/blog/2016/02/29/auto-documented-makefile.html

help:
	@grep -E '^[.0-9a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) | sort | awk 'BEGIN {FS = ":.*?## "}; {printf "\033[36m%-30s\033[0m %s\n", $$1, $$2}'

cronjobber: cronjobber.c	## Compile cronjobber
	cc -o cronjobber cronjobber.c

clean::	## remove generated files
	rm -f cronjobber cronjobber.o

# The Mac groff html generator doesn't produce output as good as the FreeBSD one.
cronjobber.8.html: cronjobber.mandoc ## generate html man page
	groff -mdoc -Thtml cronjobber.mandoc > cronjobber.8.html.new
	mv cronjobber.8.html.new cronjobber.8.html
clean::
	rm -f cronjobber.8.html cronjobber.8.html.new

cronjobber.8.txt: cronjobber.mandoc	## generate ascii man page
	nroff -mdoc cronjobber.mandoc > cronjobber.8.txt.new
	mv cronjobber.8.txt.new cronjobber.8.txt
# don't remove the .txt file because we want this in the repo for github display
clean::
	rm -f cronjobber.man.txt.new

cronjobber.8.ps: cronjobber.mandoc	## generate postscript man page
	groff -mdoc -tps cronjobber.mandoc > cronjobber.8.ps.new
	mv cronjobber.8.ps.new cronjobber.8.ps
clean::
	rm -f cronjobber.8.ps cronjobber.8.ps.new

cronjobber.8.pdf: cronjobber.8.ps	## generate PDF manpage
	pstopdf cronjobber.8.ps -o cronjobber.8.pdf.new
	mv cronjobber.8.pdf.new cronjobber.8.pdf
clean::
	rm -f cronjobber.8.pdf cronjobber.8.pdf.new

release: ## Tag this version as a release
	@test -n "$(SEMVER)" || ( echo "must set SEMVER for release"; false)
	tools/brand.sh cronjobber.c "$(SEMVER)"
	cc -o cronjobber cronjobber.c
	test -d .git || (echo "must be in git working directory"; false)
	git add version.h
	git commit -m "branded $(SEMVER)"
	git push
	git tag "$(SEMVER)"
	git push --tag


brand:	## patch git version into cronjobber.c
	tools/brand.sh cronjobber.c `tools/get-git-version.sh`
