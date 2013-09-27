#!/usr/bin/python
#
#  Combine all source and header files in source directory into
#  a single C file.
#
#  The process is not very simple or clean.  This helper is not
#  a generic or 100% correct in the general case: it just needs
#  to work for Duktape.
#
#  Overview of the process:
#
#    * Parse all relevant C and H files.
#
#    * Change all non-exposed functions and variables to "static" in
#      both headers (extern -> static) and in implementation files.
#
#    * Emit internal headers by starting from duk_internal.h (the only
#      internal header included by Duktape C files) and emulating the
#      include mechanism recursively.
#
#    * Emit all source files, removing any internal includes (these
#      should all be duk_internal.h ideally but there are a few remnants).
#      
#  At every step, source and header lines are represented with explicit
#  line objects which keep track of original filename and line.  The
#  output contains #line directives, if necessary, to ensure error
#  throwing and other diagnostic info will work in a useful manner when
#  deployed.
#
#  Making the process deterministic is important, so that if users have
#  diffs that they apply to the combined source, such diffs would apply
#  for as long as possible.
#
#  Limitations and notes:
#
#    * #defines are not #undef'd at the end of an input file, so defines
#      may bleed to other files.  These need to be fixed in the original
#      sources.
#
#    * System headers included with a certain define (like _BSD_SOURCE)
#      are not handled correctly now.
#
#    * External includes are not removed or combined: some of them are
#      inside #ifdef directives, so it would difficult to do so.  Ideally
#      there would be no external includes in individual files.

import os
import sys
import re

re_extinc = re.compile(r'^#include <(.*?)>.*$')
re_intinc = re.compile(r'^#include \"(duk.*?)\".*$')  # accept duktape.h too

class File:
	filename_full = None
	filename = None
	lines = None

	def __init__(self, filename, lines):
		self.filename = os.path.basename(filename)
		self.filename_full = filename
		self.lines = lines

class Line:
	filename_full = None
	filename = None
	lineno = None
	data = None

	def __init__(self, filename, lineno, data):
		self.filename = os.path.basename(filename)
		self.filename_full = filename
		self.lineno = lineno
		self.data = data

def read(filename):
	lines = []

	f = None
	try:
		f = open(filename, 'rb')
		lineno = 0
		for line in f:
			lineno += 1
			if len(line) > 0 and line[-1] == '\n':
				line = line[:-1]
			lines.append(Line(filename, lineno, line))
	finally:
		if f is not None:
			f.close()

	return File(filename, lines)

def findFile(files, filename):
	for i in files:
		if i.filename == filename:
			return i
	return None

def processIncludes(f):
	extinc = []
	intinc = []

	for line in f.lines:
		if not line.data.startswith('#include'):
			continue

		m = re_extinc.match(line.data)
		if m is not None:
			# external includes are kept; they may even be conditional
			extinc.append(m.group(1))
			#line.data = ''
			continue

		m = re_intinc.match(line.data)
		if m is not None:
			intinc.append(m.group(1))
			#line.data = ''
			continue

		print(line.data)
		raise Exception('cannot parse include directive')

	return extinc, intinc

def processDeclarations(f):
	for line in f.lines:
		# FIXME: total placeholder
		if line.data.startswith('int ') or line.data.startswith('void '):
			line.data = 'static ' + line.data
		elif line.data.startswith('extern int') or line.data.startswith('extern void '):
			line.data = 'static ' + line.data[7:]  # replace extern with static

def createCombined(files, extinc, intinc):
	res = []

	emit_state = [ None, None ]  # curr_filename, curr_lineno

	def emit(line):
		if isinstance(line, (str, unicode)):
			res.append(line)
			emit_state[1] += 1
		else:
			if line.filename != emit_state[0] or line.lineno != emit_state[1]:
				res.append('#line %d "%s"' % (line.lineno, line.filename))
			res.append(line.data)
			emit_state[0] = line.filename
			emit_state[1] = line.lineno + 1

	processed = {}

	# Helper to process internal headers recursively, starting from duk_internal.h
	def processHeader(f_hdr):
		#print('Process header: ' + f_hdr.filename)
		for line in f_hdr.lines:
			m = re_intinc.match(line.data)
			if m is None:
				emit(line)
				continue
			incname = m.group(1)
			if incname in [ 'duktape.h', 'duk_custom.h' ]:
				# keep a few special headers as is
				emit(line)
				continue

			#print('Include: ' + incname)
			f_inc = findFile(files, incname)
			assert(f_inc)

			if processed.has_key(f_inc.filename):
				#print('already included, skip: ' + f_inc.filename)
				emit('/* already included: %s */' % f_inc.filename)
				continue
			processed[f_inc.filename] = True

			# include file in this place, recursively
			processHeader(f_inc)

	# Process internal headers by starting with duk_internal.h
	f_dukint = findFile(files, 'duk_internal.h')
	assert(f_dukint)
	processHeader(f_dukint)

	# Mark all internal headers processed
	for f in files:
		if os.path.splitext(f.filename)[1] != '.h':
			continue
		processed[f.filename] = True

	# Then emit remaining files
	for f in files:
		if processed.has_key(f.filename):
			continue

		for line in f.lines:
			m = re_intinc.match(line.data)
			if m is None:
				emit(line)
			else:
				incname = m.group(1)
				emit('/* include removed: %s */' % incname)

	return '\n'.join(res) + '\n'

def main():
	outname = sys.argv[2]
	assert(outname)

	print 'Read input files'
	files = []
	filelist = os.listdir(sys.argv[1])
	filelist.sort()  # for consistency
	for fn in filelist:
		if os.path.splitext(fn)[1] not in [ '.c', '.h' ]:
			continue
		res = read(os.path.join(sys.argv[1], fn))
		files.append(res)
	print '%d files read' % len(files)

	print 'Process #include statements'
	extinc = []
	intinc = []
	for f in files:
		extnew, intnew = processIncludes(f)
		for i in extnew:
			if i in extinc:
				continue
			extinc.append(i)
		for i in intnew:
			if i in intinc:
				continue
			intinc.append(i)

	#print('external includes: ' + ', '.join(extinc))
	#print('internal includes: ' + ', '.join(intinc))

	print 'Process declarations (non-exposed are converted to static)'
	for f in files:
		#processDeclarations(f)
		pass

	print 'Output final file'
	final = createCombined(files, extinc, intinc)
	f = open(outname, 'wb')
	f.write(final)
	f.close()

	print 'Wrote %d bytes to %s' % (len(final), outname)

if __name__ == '__main__':
	main()

