NB. 9!:27 'exit 1'
NB. 9!:29]1
require 'csv'

d=:'/Users/rauldmiller/github/rdm/geoh/'

ip2l=: 0 1 1 0 1 0#"1 readcsv d,'ip2location/IP-COUNTRY-REGION-CITY'
dstate=:|:readcsv d,'d_state'
ip2l=:ip2l,.(((<'US')=1{"1 ip2l)*({.dstate)i.2{"1 ip2l){{:dstate
ip2l writecsv d,'ip-country-state'
ip=:{."1 ip2l
ref=:}."1 ip2l
ip2l=:''
nub=:/:~~.ref
nub writecsv d,'country-state'
(ip,.<"0 nub i. ref) writecsv d,'ip-nub'
exit 0
