main: main.c timeout.h mapdate.h refdata.h localdata.h
	cc $@.c -o $@

distribute: main ip.map.bz2 ip.map.gz distribute.sh local
	chmod +x testlocal distribute.sh testremote
	./testlocal
	./distribute.sh
	. $$HOME/.ssh-agent.sh && ssh -v ubuntu@54.204.234.199 sh src/deployprod
	./testremote

mapdate.h: buildmapdate
	./$<

refdata.h: buildrefdata country-state.csv timeout.h
	chmod +x buildrefdata
	./buildrefdata country-state.csv >$@
	grep US $@ >/dev/null || rm $@ $@

localdata.h: buildlocaldata country-state.csv timeout.h
	chmod +x buildlocaldata
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
	chmod +x buildmap
	rm -f ip.map.new
	./buildmap

ip.map: ip.map.new
	rm -f ip.map
	ln ip.map.new ip.map

ip.map.gz: ip.map
	gzip -9c <ip.map >t.gz
	mv t.gz ip.map.gz

ip.map.bz2: ip.map
	bzip2 -c <ip.map >t.bz2
	mv t.bz2 ip.map.bz2

local: local.c

testlocal.test: local testlocal
	./testlocal >$@

testremote.test: local testremote
	./testremote >$@
