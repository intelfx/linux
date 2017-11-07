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
	new_kwargs = {
		'check': True,
		'universal_newlines': True,
		**kwargs,
	}
	new_kwargs.update(kwargs)
	return subprocess.run(
		args=args,
		**new_kwargs,
	)


def run_stdout(args, **kwargs):
	new_kwargs = {
		'stdout': subprocess.PIPE,
		**kwargs,
	}
	new_kwargs.update(kwargs)
	return run(
		args=args,
		**new_kwargs,
	).stdout.rstrip('\n')


def Popen(args, **kwargs):
	new_kwargs = {
		'universal_newlines': True,
		**kwargs,
	}
	new_kwargs.update(kwargs)
	return subprocess.Popen(
		args=args,
		**new_kwargs,
	)


def Popen_stdout(args, **kwargs):
	new_kwargs = {
		'stdout': subprocess.PIPE,
		**kwargs,
	}
	new_kwargs.update(kwargs)
	return Popen(
		args=args,
		**new_kwargs,
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
git_describe = run_stdout([ 'git', 'describe', '--tags' ])
(git_upstream,
 git_revcount,
 git_revid) = git_describe_regex.fullmatch(git_describe).groups()

#
# Find out the buildsystem's version.
#

pkg_version_regex = re.compile('(\d+\.\d+\.\d+)-(.+)')
pkg_version = run_stdout([ 'dpkg-parsechangelog', '-SVersion' ])
(pkg_upstream,
 pkg_revision) = pkg_version_regex.fullmatch(pkg_version).groups()

#
# Get the package name, for consistency.
#

pkg_name = run_stdout([ 'dpkg-parsechangelog', '-SSource' ])

with \
	open('debian/changelog', 'r') as changelog, \
	open('debian/changelog-new', 'w') as changelog_new, \
	Popen([
			'git',
			'log',
			f'v{git_upstream}..',
			'--pretty=tformat:' + '\x1f'.join([
				'%h',
				'%s',
				'%cn',
				'%ce',
				'%cD',
			])
		],
		stdout=subprocess.PIPE,
	) as gitlog:

	changelog_new.write(f'''
{pkg_name} ({git_upstream}-tfw{git_revcount}-{pkg_revision}) UNRELEASED; urgency=medium

  * TempestaFW kernel v{git_upstream}-{git_revcount}-g{git_revid}:
'''.lstrip('\n'))
	
	for line in reversed(list(gitlog.stdout)):
		(gitlog_revid,
		 gitlog_subject,
		 gitlog_name,
		 gitlog_email,
		 gitlog_date) = line.split('\x1f')
		changelog_new.write(f'''
    - {gitlog_subject}
'''.lstrip('\n'))

	changelog_new.write(f'''
 -- {gitlog_name} <{gitlog_email}>  {gitlog_date}
''')  # no lstrip('\n') on purpose

	changelog_new.write(changelog.read())

os.rename('debian/changelog-new', 'debian/changelog')

#
# Generate the control file (it exits with rc=1)
#
run([ 'debian/rules', 'debian/control' ], check=False)

#
# Run modified orig target to apply patches to the existing working tree
#

run([ 'debian/rules', 'orig' ])
