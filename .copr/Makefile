# Copr "make srpm" source method (Package > SCM > "make srpm"): Copr clones the repository and runs
#   make -f .copr/Makefile srpm outdir=<dir> spec=<spec>
# in a mock chroot with network access; the RPM itself is built from the resulting SRPM offline.
outdir ?= $(CURDIR)/package/early-release-packages

srpm:
	dnf -y install git-core rpm-build tar gzip sed
	git config --global --add safe.directory '*'
	./package/fedora/make-srpm.sh "$(outdir)"

.PHONY: srpm
