#!/bin/sh
# Copyright 1994, Animorphic Systems. $Revision: 1.3 $
# Converted from CCompare.bat; executable from /bin/sh (delegates to Perl).
# Note: windiff is a Windows tool and may not be available on this system.
case "$0" in
  /*) _script="$0" ;;
  *)  _script="$PWD/$0" ;;
esac
cd "$1" || exit $?
exec perl -S -x "$_script" "$@"
#! /usr/bin/perl
use strict;
use warnings;

# $CWD, $Delta and $Baseline are defined by delta.pl (required below).
our $CWD;
our $Delta;
our $Baseline;

require "delta.pl";

our $file = $ARGV[1];

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
`windiff $ARGV[0]\\$ARGV[1] $Baseline\\$sub\\$ARGV[1]`;
__END__
