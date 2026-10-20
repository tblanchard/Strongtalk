#!/bin/sh
# Copyright 1994, Animorphic Systems. $Revision: 1.3 $
# Converted from publishAll.bat; executable from /bin/sh (delegates to Perl).
exec perl -S -x "$0" "$@"
#! /usr/bin/perl
use strict;
use warnings;

open(my $files, '-|', 'baseVMfiles') or die "Can't run baseVMfiles: $!";
while (my $line = <$files>) {
  chomp $line;
  print STDERR '.';
  `basePublish $line`;
}
close $files;
__END__
