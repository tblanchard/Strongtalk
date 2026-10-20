#!/bin/sh
# Copyright 1994, Animorphic Systems. $Revision: 1.1 $
# Converted from compareMatchingFiles.bat; executable from /bin/sh (delegates to Perl).
exec perl -S -x "$0" "$@"
#! /usr/bin/perl
use strict;
use warnings;

our $remote = $ARGV[0];

opendir(my $dh, '.') or warn "Can't open .: $!\n";
my @filenames = readdir($dh);
closedir($dh);
my %localFiles;
for my $name (@filenames) {
  next if $name eq '.';
  next if $name eq '..';
  if (-f $name) {
    $localFiles{$name} = 1;
  }
}

opendir($dh, $remote) or warn "Can't open $remote: $!\n";
@filenames = readdir($dh);
closedir($dh);
my %baseFiles;
for my $name (@filenames) {
  next if $name eq '.';
  next if $name eq '..';
  if (-f "$remote/$name") {
    $baseFiles{$name} = 1;
  }
}

for my $name (sort keys %localFiles) {
  if ($baseFiles{$name}) {
    print "\@diff -C 2 $remote/$name $name\n";
  }
}
__END__
