#!/usr/bin/python
#
#  Generate build parameter files based on build information.
#  A C header is generated for C code, and a JSON file for
#  build scripts etc which need to know the build config.
#

import os
import sys
import json
import optparse

import dukutil

if __name__ == '__main__':
	parser = optparse.OptionParser()
	parser.add_option('--version', dest='version')
	parser.add_option('--build', dest='build')
	parser.add_option('--out-json', dest='out_json')
	parser.add_option('--out-header', dest='out_header')
	(opts, args) = parser.parse_args()

	t = { 'version': opts.version, 'build': opts.build }

	f = open(opts.out_json, 'wb')
	f.write(dukutil.json_encode(t).encode('ascii'))
	f.close()

	f = open(opts.out_header, 'wb')
	f.write('#ifndef  DUK_BUILDPARAMS_H_INCLUDED\n')
	f.write('#define  DUK_BUILDPARAMS_H_INCLUDED\n')
	f.write('/* automatically generated by genbuildparams.py, do not edit */\n')
	f.write('\n')
	f.write('#define  DUK_VERSION     %s\n' % opts.version)
	f.write('#define  DUK_BUILD       "%s"\n' % opts.build)
	f.write('\n')
	f.write('#endif  /* DUK_BUILDPARAMS_H_INCLUDED */\n')
	f.close()

