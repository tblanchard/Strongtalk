#!/bin/sh
# Copyright 1994, Animorphic Systems. $Revision: 1.4 $
# Converted from compare.bat; executable from /bin/sh (delegates to Perl).
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

chomp(my $CWD = `cd`);
$CWD       = dirname($CWD);
our $Delta = dirname($ENV{'DeltaDir'});

our $file = $ARGV[0];

sub deltaSubDir {
  if ($CWD eq $Delta) {
    return '.';
  }
  my $res = rindex("$CWD", "$Delta", 0);
  if ($res == -1) {
    die "You must be in a sub directory of $Delta!";
  }
  return substr($CWD, 1 + length($Delta) - length($CWD));
}

our $sub = deltaSubDir();
`baseCompare $sub $file`;
__END__
