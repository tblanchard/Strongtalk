#!/bin/sh
# Copyright 1994, Animorphic Systems. $Revision: 1.2 $
# Converted from filter.bat; executable from /bin/sh (delegates to Perl).
exec perl -S -x "$0" "$@"
#! /usr/bin/perl
use strict;
use warnings;

our $file_name = $ARGV[0];

sub getStatFor {
  my $name = shift;
  open(my $fh, '<', $file_name) or die "could not open file: $file_name";
  print "$name\t";
  my $mm = 9999999;
  while (my $line = <$fh>) {
    chomp $line;
    if ($line =~ /^$name\t([0-9]*)/) {
      if ($mm > $1) {
        $mm = $1;
      }
    }
  }
  print "$mm\n";
  close($fh);
  return '';
}

sub getSmopFor {
  my $name = shift;
  open(my $fh, '<', $file_name) or die "could not open file: $file_name";
  print "$name\t";
  my $mm = 999999;
  my $nn = 0;
  while (my $line = <$fh>) {
    chomp $line;
    if ($line =~ /^(\d*(\.\d*)?)     (\d*(\.\d*)?)     $name/) {
      if ($mm > $1) {
        $mm = $1;
        $nn = $3;
      }
    }
  }
  print "$mm\t$nn\n";
  close($fh);
  return '';
}

sub getSlopFor {
  my $name = shift;
  open(my $fh, '<', $file_name) or die "could not open file: $file_name";
  print "$name\t";
  my $mm = 9999999;
  my $nn = 0;
  while (my $line = <$fh>) {
    chomp $line;
    if ($line =~ /^\d*     (\d*(\.\d*)?)     \d*     (\d*(\.\d*)?)     $name/) {
      if ($mm > $1) {
        $mm = $1;
        $nn = $3;
      }
    }
  }
  print "$mm\t$nn\n";
  close($fh);
  return '';
}

getStatFor("BubbleSort");
getStatFor("BubbleSort2");
getStatFor("IntMM");
getStatFor("IntMM2");
getStatFor("MM");
getStatFor("MM2");
getStatFor("Perm");
getStatFor("Perm2");
getStatFor("Queens");
getStatFor("Queens2");
getStatFor("Quicksort");
getStatFor("Quicksort2");
getStatFor("Towers");
getStatFor("Towers2");
getStatFor("TreeSort");
getStatFor("TreeSort2");
getStatFor("Puzzle");
getStatFor("Sieve");
getStatFor("SumTo");
getStatFor("Recurse");
getStatFor("AtAllPut");
getStatFor("IncrementAll");
getStatFor("NestedLoop");
getStatFor("Tak");
getStatFor("Takl");
getStatFor("Dictionary");
getStatFor("OrderedCollection");
getStatFor("Livermore");

print "\n";
getStatFor("Richards");

print "\n";
getStatFor("DeltaBlue");

print "\n";
getSlopFor("add integers");
getSlopFor("add floats");
getSlopFor("access strings");
getSlopFor("create objects");
getSlopFor("copy objects");
getSlopFor("perform selectors");
getSlopFor("evaluate blocks");

print "\n";
getSmopFor("generating fractonaccis");
getSmopFor("generating primes");
getSmopFor("generating and parsing streams");
getSmopFor("generating strings");
getSmopFor("forming sets");
getSmopFor("sorting strings");
getSmopFor("sorcerer's apprentice");
__END__
