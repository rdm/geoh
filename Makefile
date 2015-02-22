main: main.c good.h
	cc $@.c -o $@

all: main ip.map.bz2 ip.map.gz
	./deploy

good.h: buildresponse country-state.csv 
	./buildresponse country-state.csv >$@

buildmap:

ip-nub.csv: ip2location/IP-COUNTRY-REGION-CITY.CSV
	jconsole refine-csv.ijs

ip.map: buildmap country-state.csv ip-nub.csv
	./buildmap

ip.map.gz: ip.map
	gzip -9c <ip.map >t.gz
	mv t.gz ip.map.gz

ip.map.bz2: ip.map
	bzip -c <ip.map >t.bz2
	mv t.bz2 ip.map.bz2
