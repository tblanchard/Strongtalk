#!/bin/sh
# Copyright 1994, Animorphic Systems. $Revision: 1.4 $
# Converted from checkvmFiles.bat; executable from /bin/sh (delegates to Perl).
exec perl -S -x "$0" "$@"
#! /usr/bin/perl
use strict;
use warnings;

my %localFiles;
my %baseFiles;

open(my $fh, '-|', 'baseVMfiles') or die "Can't run baseVMfiles: $!";
while (my $line = <$fh>) {
  chomp $line;
  my ($dir, $file) = split(/ /, $line);
  my $name = "$dir/$file";
  $localFiles{$name} = 1;
}
close $fh;

open($fh, '-|', 'baseBLfiles') or die "Can't run baseBLfiles: $!";
while (my $line = <$fh>) {
  chomp $line;
  my ($dir, $file) = split(/ /, $line);
  my $name = "$dir/$file";
  $baseFiles{$name} = 1;
}
close $fh;

for my $name (sort keys %localFiles) {
  print STDERR "$name (new locally)\n" unless $baseFiles{$name};
}

for my $name (sort keys %baseFiles) {
  print STDERR "\t$name (new in baseline)\n" unless $localFiles{$name};
}
__END__
