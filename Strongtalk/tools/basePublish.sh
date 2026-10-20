#!/bin/sh
# Copyright 1994, Animorphic Systems. $Revision: 1.4 $
# Converted from basePublish.bat; executable from /bin/sh (delegates to Perl).
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
our $rcsfile   = "$Baseline/$sub/RCS/$file,v";

sub checkIn {
  my ($dir, $file) = @_;
  my $rev_number = 1;
  my $rev_name   = 'Genesis';

  chdir($dir);
  if (-f "RCS/$file,v") {
    `rcs -U -q $file`;
  }
  `ci -u$rev_number -m$rev_number -N$rev_name -q -t- $file`;
}

sub needDirectory {
  my $dir = shift;
  unless (-d $dir) {
    mkdir($dir, 0777) or die "Couldn't create $dir!\n";
  }
}

unless (-f $localfile) {
  die "Local file: $localfile does not exist!\n";
}

needDirectory("$Baseline/$sub");
needDirectory("$Baseline/$sub/RCS");

sub doit {
  print STDERR "\nPublish: $file in $sub.\n";
  chmod(0666, $basefile);
  `cp $localfile $localfile.publish`;
  `cp $localfile $basefile`;
  checkIn("$Baseline/$sub", $file);
  chmod(0444, $basefile);
  `cp $basefile $localfile`;
  chmod(0666, $localfile);
}

unless (-f $basefile) {
  doit();
  exit(0);
}

chomp(my $result = `diff $basefile $localfile`);
exit(0) if $result eq '';

sub reverse {
  my $name = shift;
  $name =~ s/\//\\/g;
  return $name;
}

my $r_basefile  = reverse($basefile);
my $r_localfile = reverse($localfile);

my $base_rev  = `getRevision $r_basefile`;
my $local_rev = `getRevision $r_localfile`;

if ($base_rev eq $local_rev) {
  doit();
} else {
  print STDERR "Revision number in the local file conflicts\n";
  print STDERR "with the revision number in the baseline file\n";
  print STDERR "Local:    $localfile ($local_rev)\n";
  print STDERR "Baseline: $basefile  ($base_rev)\n";
}
__END__
