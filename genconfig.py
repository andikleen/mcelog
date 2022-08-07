#!/usr/bin/env python3
# generate man config documentation from mcelog.conf example
# genconfig.py mcelog.conf intro.html
import sys
import re
import argparse

ap = argparse.ArgumentParser(description="generate man config documentation from mcelog.conf example")
ap.add_argument('config', type=argparse.FileType('r'), help="mcelog example config file")
ap.add_argument('intro', type=argparse.FileType('r'), help="intro file")
args = ap.parse_args()

def parse(f):
  lineno = 1
  explanation = 0
  header = 1
  for line in f:
    lineno += 1

    # skip first comment
    if header:
      if not re.match('^#', line):
        header = 0
      continue

    # explanation
    m = re.match('^#\s(.*)', line)
    if m:
      explanation += 1
      s = m.group(1)
      if explanation == 1:
        s = s.capitalize()
      print(s)
      continue

    if explanation:
      print(".PP")
      explanation = 0

    # empty line: new option
    if re.match('\s+', line):
      new_option()
      continue
    # group
    m = re.match('\[(.*)\]', line)
    if m:
      start_group(m.group(1))
      continue
    # config option
    m = re.match('^(#?)([a-z-]+) = (.*)', line)
    if m:
      config_option(m.group(1), m.group(2), m.group(3))
      continue
    print("Unparseable line %d" % (lineno-1), file=sys.stderr, flush=True)

def config_option(enabled, name, value):
    print(".B %s = %s" % (name, value))
    print(".PP")

def start_group(name):
    print(".SS \"The %s config section\"" % (name))

def new_option():
    print(".PP")


print("""
.\\" Auto generated mcelog.conf manpage. Do not edit.
.TH "mcelog.conf" 5 "mcelog"
""")

print(args.intro.read())
parse(args.config)
print("""
.SH SEE ALSO
.BR mcelog (8),
.BR mcelog.triggers (5)
.B http://www.mcelog.org
""")
