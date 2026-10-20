#!/bin/sh
# Copyright 1994, Animorphic Systems. $Revision: 1.5 $
# Converted from baseVMFiles.bat; executable from /bin/sh (delegates to Perl).
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
  my $dir = shift;
  opendir(my $dh, "$Delta/$dir") or do { warn "Can't open $dir: $!\n"; return };
  my @filenames = readdir($dh);
  closedir($dh);
  for my $name (@filenames) {
    next if $name eq '.';
    next if $name eq '..';
    if (-d "$Delta/$dir/$name") {
      files(catdir($dir, $name));
    } else {
      wanted($dir, $name);
    }
  }
}

sub wanted {
  my ($dir, $file) = @_;
  return if $file =~ /vmDate.cpp/;
  print "$dir $file\n" if $file =~ /\.(?:hpp|cpp|pl|bat|asm)$/
    || $file =~ /[mM]akefile$/
    || $file =~ /prims\.src$|prims\.inc$/
    || $file =~ /includeDB(?:2)?$/
    || $file =~ /\.(?:cls|dlt|gr|str)$/;
}

sub check_dir {
  my $sub = shift;
  my $dir = catdir($Delta, $sub);
  return unless -d $dir;
  $dir = catdir($Baseline, $sub);
  return unless -d $dir;
  files($sub);
}

check_dir("bin");
check_dir("vm");
check_dir("world");
__END__
