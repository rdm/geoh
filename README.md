# geoh
geo lookup tools

Current status: sketchy (I'm still writing the initial version)

refine-csv.ijs translates ip2location db3 data set to something which can be injested by buildmap

buildmap translates ip-nub.csv (from refine-csv) to a map intended for use by main.c

buildresponse translates country-state.csv to c to be incorporated into main

main acts as an ip-lookup web server
