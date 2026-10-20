#!/bin/sh
# Copyright 1994, Animorphic Systems. $Revision: 1.3 $
# Converted from getRevision.bat; executable from /bin/sh (delegates to Perl).
exec perl -S -x "$0" "$@"
#! /usr/bin/perl
use strict;
use warnings;

our $name = $ARGV[0];

sub getRevision {
  my $filename = shift;
  open(my $fh, '<', $filename) or die "could not open file: $filename";
  while (my $line = <$fh>) {
    if ($line =~ /\Revision:\s*(\S+)\s+/) {
      close($fh);
      return $1;
    }
  }
  close($fh);
  return '';
}

print getRevision($name);
__END__
