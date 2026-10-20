#!/bin/sh
# Copyright 1994, Animorphic Systems. $Revision: 1.3 $
# Converted from baseBLFiles.bat; executable from /bin/sh (delegates to Perl).
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

our $Baseline = dirname($ENV{'DeltaBaselineDir'});
our $Delta    = dirname($ENV{'DeltaDir'});

sub catdir {
  my ($a, $b) = @_;
  return $a if $b eq '';
  return $b if $a eq '';
  return "$a/$b";
}

sub files {
  my $sub = shift;
  my $dir = catdir($Baseline, $sub);
  opendir(my $dh, $dir) or do { warn "Can't open $dir: $!\n"; return };
  my @filenames = readdir($dh);
  closedir($dh);
  for my $name (@filenames) {
    next if $name eq '.';
    next if $name eq '..';
    next if $name eq 'RCS';
    if (-d "$dir/$name") {
      files(catdir($sub, $name));
    } else {
      wanted($dir, $name);
    }
  }
}

sub wanted {
  my ($dir, $file) = @_;
  (my $rel = $dir) =~ s/^\Q$Delta\E\/?//;
  print "$rel $file\n";
}

sub check_dir {
  my $sub = shift;
  my $dir = catdir($Delta, $sub);
  if (-d $dir) {
    files($sub);
  }
}

check_dir("bin");
check_dir("vm");
check_dir("world");
__END__
