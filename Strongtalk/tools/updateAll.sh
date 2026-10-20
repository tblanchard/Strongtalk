#!/bin/sh
# Copyright 1994, Animorphic Systems. $Revision: 1.3 $
# Converted from updateAll.bat; executable from /bin/sh (delegates to Perl).
exec perl -S -x "$0" "$@"
#! /usr/bin/perl
use strict;
use warnings;

open(my $files, '-|', 'baseBLFiles') or die "Can't run baseBLFiles: $!";
while (my $line = <$files>) {
  chomp $line;
  print STDERR '.';
  `baseUpdate $line`;
}
close $files;
__END__
