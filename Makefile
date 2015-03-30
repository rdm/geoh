main: main.c timeout.h mapdate.h refdata.h localdata.h
	cc $@.c -o $@

distribute: main ip.map.bz2 ip.map.gz distribute.sh
	./testlocal.test
	./distribute.sh
	. $$HOME/.ssh-agent.sh && ssh -v ubuntu@54.204.234.199 sh src/deployprod
	./testremote.test

mapdate.h: buildmapdate
	./$<

refdata.h: buildrefdata country-state.csv timeout.h
	./buildrefdata country-state.csv >$@
	grep US $@ >/dev/null || rm $@ $@

localdata.h: buildlocaldata country-state.csv timeout.h
	./buildlocaldata country-state.csv >$@
	grep US $@ >/dev/null || rm $@ $@

buildmap: buildmap.c

# at the begining of each month
# download DB3-IP-COUNTRY-REGION-CITY.CSV.ZIP
# from https://www.ip2location.com/file-download
ip2location/IP-COUNTRY-REGION-CITY.CSV: ip2location/DB3-IP-COUNTRY-REGION-CITY.CSV.ZIP
	cd ip2location && unzip -o DB3-IP-COUNTRY-REGION-CITY.CSV.ZIP

ip-country-state.csv: ip2location/IP-COUNTRY-REGION-CITY.CSV
	perl refine-csv.pl
	mv ip-country-state.csv.tmp ip-country-state.csv

ip-nub.csv: ip-country-state.csv
	mv $@.tmp $@

country-state.csv: ip-country-state.csv
	mv $@.tmp $@

ip.map.new: buildmap country-state.csv ip-nub.csv
	./buildmap

ip.map.gz: ip.map.new
	gzip -9c <ip.map.new >t.gz
	mv t.gz ip.map.gz

ip.map.bz2: ip.map.new
	bzip2 -c <ip.map >t.bz2
	mv t.bz2 ip.map.bz2

ip.map: ip.map.bz2
	bzip -dc <ip.map.bz2 >t.bz2
	mv t.bz2 ip.map

local: local.c

testlocal.test: local testlocal
	./testlocal >$@

testremote.test: local testremote
	./testremote >$@
