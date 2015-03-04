#!/usr/bin/perl
use Text::CSV;

# ok actually this is about line ending character(s)...
my $parser= Text::CSV->new({binary=>1, eol=>"\r\n"}) or die Text::CSV->error_diag();
my $writer= Text::CSV->new({binary=>1, eol=>"\n"}) or die Text::CSV->error_diag();

open my $csv, 'd_state.csv' or die $!;
while (my $row= $writer->getline($csv)) {
	my ($name, $code)= @$row;
	$dstate{$name}= $code;
}
close $csv;

my @topip=();
my @ccode=();
my @state=();
my @scode=();
open my $IP2Lcsv, 'ip2location/IP-COUNTRY-REGION-CITY.csv' or die $!;
open my $icscsv, '>ip-country-state.csv.tmp' or die $!;
while (my $row= $parser->getline($IP2Lcsv)) {
	my ($lowip, $topip, $ccode, $cname, $rname, $city)= @$row;
	my $rcode= '-';
	if ('US' eq $ccode) {
		$rcode= $dstate{$rname} or die "no state code for '$rname'";
	}
	$cs{qq{"$ccode","rname","rcode"}}= [$ccode, $rname, $rcode];
	$writer->print($icscsv, [$topip, $ccode, $rname, $rcode]);
}
close $IP2Lcsv;
close $icscsv;

$ndx= 0;
open my $cscsv, '>country-state.csv.tmp' or die $!;
for $cs (sort keys %cs) {
	$nub{$cs}= $ndx++;
	$writer->print($cscsv, $cs{$cs});
}
close $cscsv;

open $IP2Lcsv, 'ip2location/IP-COUNTRY-REGION-CITY.csv' or die $!;
open my $nubcsv, '>ip-nub.csv.tmp' or die $!;
while (my $row= $parser->getline($IP2Lcsv)) {
	my ($lowip, $topip, $ccode, $cname, $rname, $city)= @$row;
	my $rcode= '-';
	if ('US' eq $ccode) {
		$rcode= $dstate{$rname} or die "no state code for '$rname'";
	}
	my $ndx= $nub{qq{"$ccode","rname","rcode"}};
	$writer->print($nubcsv, [$topip, $ndx]);
}
close $IP2Lcsv;
close $nubcsv;
