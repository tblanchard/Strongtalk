#!/bin/sh
# Copyright 1994, Animorphic Systems. $Revision: 1.3 $
# Converted from primDefFilter.bat; executable from /bin/sh (delegates to Perl).
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

our $Directory = "$Delta/vm/prims";

opendir(my $dh, $Directory) or die "Can't open vm/prims!\n";
my @filenames = readdir($dh);
closedir($dh);

sub generate {
  my $filename = shift;
  my $inside   = 0;
  open(my $fh, '<', "$Directory/$filename") or die "Can't open $filename";
  for my $line (<$fh>) {
    chomp $line;    # remove the newline
    if ($inside) {
      if ($line =~ /^[ \t]*\/\/%/) {
        print "!\n";
        $inside = 0;
      } else {
        if ($line =~ /^[ \t]*\/\/(.*)/) {
          print "$1\n";
        } else {
          die "Wrong format inside primitive definition";
        }
      }
    } else {
      if ($line =~ /^[ \t]*\/\/%prim/) {
        $inside = 1;
      }
    }
  }
  if ($inside) {
    die "Primitive definition not completed";
  }
  close($fh);
}

for my $filename (@filenames) {
  generate($filename) if $filename =~ /[a-zA-Z]+_prims.hpp$/;
}
print "!\n";
__END__
