main: main.c timeout.h mapdate.h refdata.h localdata.h
	cc $@.c -o $@

all: main ip.map.bz2 ip.map.gz
	./deploy

mapdate.h: buildmapdate
	./$<

refdata.h: buildrefdata country-state.csv timeout.h
	./buildrefdata country-state.csv >$@
	grep US $@ >/dev/null || rm $@ $@

localdata.h: buildlocaldata country-state.csv timeout.h
	./buildlocaldata country-state.csv >$@
	grep US $@ >/dev/null || rm $@ $@

buildmap: buildmap.c

ip-nub.csv: ip2location/IP-COUNTRY-REGION-CITY.CSV
	jconsole refine-csv.ijs

ip.map.new: buildmap country-state.csv ip-nub.csv
	./buildmap

ip.map.gz: ip.map.new
	gzip -9c <ip.map.new >t.gz
	mv t.gz ip.map.gz

ip.map.bz2: ip.map.new
	bzip -c <ip.map >t.bz2
	mv t.bz2 ip.map.bz2

ip.map: ip.map.bz2
	bzip -dc <ip.map.bz2 >t.bz2
	mv t.bz2 ip.map
