#!/usr/bin/env python3

#
# This is a kernel packaging script for Debian 9.
# It (ab)uses the native Debian kernel buildsystem in .ci/debian9/pkg.
# The modus operandi consists in automatically "bumping" the buildsystem
# to the release found in the source tree, and then synthesizing the changelog
# entries for each new change found since the release. We assume that all
# patches are compatible with the new release.
# TODO: use the buildsystem as-is, instead exporting the changes since vanilla
# as patches of a separate featureset.
#

import sys
import os
import os.path as p
import subprocess
import errno
import re

def run(args, **kwargs):
	return subprocess.run(
		args=args,
		check=True,
		universal_newlines=True,
		**kwargs,
	)

def Popen(args, **kwargs):
	return subprocess.Popen(
		args=args,
		universal_newlines=True,
		**kwargs,
	)

def unlink_force(*args, **kwargs):
	try:
		os.unlink(*args, **kwargs)
	except OSError as e:
		if e.errno == errno.ENOENT:
			pass
		else:
			raise


# For some reason, these are part of neither build-essential nor devscripts,
# yet required for generating debian/control and running `mk-build-deps`.
# We don't want to use `apt-get build-dep linux` as this feels wrong
# (we are building a local package whose buildsystem only matches that of linux
#  by accident).
pre_deps = [
	'kernel-wedge',
	'equivs',
]

run([
	'apt-get', '-y',
	'install',
	*pre_deps
])

if not p.exists('.ci'):
	raise RuntimeError(f'This script must be run from the repo root.')

unlink_force('debian')
os.symlink('.ci/debian9/pkg/debian', 'debian')

#
# Find out our version.
#

git_describe_regex = re.compile('v(\d+\.\d+\.\d+)-(\d+)-g([0-9a-fA-F]+)')
git_describe = run(
	[ 'git', 'describe', '--tags' ],
	stdout=subprocess.PIPE
).stdout.rstrip('\n')
(git_upstream,
 git_revcount,
 git_revid) = git_describe_regex.fullmatch(git_describe).groups()

print(f'git upstream: {git_upstream} git revcount: {git_revcount} git revid: {git_revid}')

#
# Find out the buildsystem's version.
#

version_regex = re.compile('(\d+\.\d+\.\d+)-(.+)')
version = run(
	[ 'dpkg-parsechangelog', '-SVersion' ],
	stdout=subprocess.PIPE
).stdout.rstrip('\n')
(version_upstream,
 version_debianrevision) = version_regex.fullmatch(version).groups()

print(f'version upstream: {version_upstream} version debianversion: {version_debianrevision}')

with \
	open('debian/changelog', 'r') as changelog, \
	open('debian/changelog-new', 'w') as changelog_new, \
	Popen([
			'git',
			'log',
			f'v{git_upstream}..',
			'--pretty=tformat:' + '\xff'.join([
				'%h',
				'%s',
				'%cn',
				'%ce',
				'%cD',
			])
		],
		stdout=subprocess.PIPE,
	) as gitlog:
	for line, revnr in zip(gitlog.stdout, range(int(git_revcount, 1, -1)):
		(gitlog_revid,
		 gitlog_subject,
		 gitlog_name,
		 gitlog_email,
		 gitlog_date) = line.split('\xff')
		changelog_new.write(f'''
 * {gitlog_subject}
