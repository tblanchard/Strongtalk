#!/bin/sh
# Copyright 1994, Animorphic Systems. $Revision: 1.3 $
# Converted from baseCompare.bat; executable from /bin/sh (delegates to Perl).
exec perl -S -x "$0" "$@"
#! /usr/bin/perl
use strict;
use warnings;

our $sub  = $ARGV[0];
our $file = $ARGV[1];

sub dirname {
  my $name = shift;
  $name =~ s/\\/\//g;
  $name =~ tr/A-Z/a-z/;
  return $name;
}

our $Baseline  = dirname($ENV{'DeltaBaselineDir'});
our $Delta     = dirname($ENV{'DeltaDir'});

our $localfile = "$Delta/$sub/$file";
our $basefile  = "$Baseline/$sub/$file";

die "Local file: $localfile does not exist!\n" unless -f $localfile;
die "Base file:  $basefile does not exist!\n"  unless -f $basefile;

chomp(my $result = `cmp $basefile $localfile`);
if ($result ne '') {
  print STDERR "\n[$sub/$file]\n";
  open(my $lines, '-|', "diff $basefile $localfile") or die "Can't run diff: $!";
  while (my $line = <$lines>) {
    chomp $line;
    print STDERR "  $line\n";
  }
  close $lines;
}
__END__
