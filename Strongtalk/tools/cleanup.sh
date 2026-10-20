#!/bin/sh
# Copyright 1994, Animorphic Systems. $Revision: 1.3 $
# Converted from cleanup.bat; executable from /bin/sh (delegates to Perl).
exec perl -S -x "$0" "$@"
#! /usr/bin/perl
use strict;
use warnings;

sub dirname {
  my $name = shift;
  $name =~ s/\\/\//g;
  $name =~ tr/A-Z/a-z/;
  return $name;
}

our $Delta = dirname($ENV{'DeltaDir'});

sub files {
  my $dir = shift;
  opendir(my $dh, $dir) or do { warn "Can't open $dir: $!\n"; return };
  my @filenames = readdir($dh);
  closedir($dh);
  for my $name (@filenames) {
    next if $name eq '.';
    next if $name eq '..';
    if (-d "$dir/$name") {
      files("$dir/$name");
    } else {
      wanted("$dir/$name");
    }
  }
}

sub mydelete {
  my $file = shift;
  print "Deleting $file\n";
  unlink $file;
}

sub wanted {
  my $file = shift;
  mydelete($file) if $file =~ /\.(?:bak|OLD|publish|~)$/;
}

files($Delta);
__END__
