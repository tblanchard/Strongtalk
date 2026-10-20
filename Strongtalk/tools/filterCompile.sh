#!/bin/sh
# Copyright 1996, Animorphic Systems. $Revision: 1.1 $
# Converted from filterCompile.bat; executable from /bin/sh (delegates to Perl).
exec perl -S -x "$0" "$@"
#! /usr/bin/perl
use strict;
use warnings;

our $file_name = $ARGV[0];

sub getStats {
  open(my $fh, '<', $file_name) or die "could not open file: $file_name";
  my $int      = 0;
  my $compiled = 0;
  my $compiler = 0;
  my $total    = 0;
  my $rest     = 0;
  my $first    = 1;
  while (my $line = <$fh>) {
    chomp $line;
    if ($line =~ /^Flatprofile (\d*(\.\d*)?) secs \((\d*) ticks\)/) {
      unless ($first) {
        $rest = $total - $int - $compiled - $compiler;
        printf "%d\t%d\t%d\t%d\n", $rest, $int, $compiled, $compiler;
      }
      $first = 0;
      $total  = $3;
      $int    = $compiler = $compiled = 0;
    }
    if ($line =~ /(\d*(\.\d*)?)\% *(\d*) Total interpreted/) {
      $int = $3;
    }
    if ($line =~ /(\d*(\.\d*)?)\% *(\d*) Total compiled/) {
      $compiled = $3;
    }
    if ($line =~ /(\d*(\.\d*)?)\% *(\d*) Total compilation/) {
      $compiler = $3;
      #exit(0);
    }
  }
  close($fh);
  unless ($first) {
    printf "%d\t%d\t%d\t%d\n", $total, $int, $compiled, $compiler;
  }
}

getStats();
__END__
