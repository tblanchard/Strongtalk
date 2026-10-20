#!/bin/sh
# Copyright 1994, Animorphic Systems. $Revision: 1.3 $
# Converted from rmfiles.bat; executable from /bin/sh (delegates to Perl).
exec perl -S -x "$0" "$@"
#! /usr/bin/perl
use strict;
use warnings;

while (defined(my $line = <STDIN>)) {
  chomp $line;
  print STDERR "  deleting $line\n";
  unlink $line;
}
__END__
