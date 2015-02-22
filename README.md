# geoh
geo lookup tools

Current status: working

Why:
---

In my current job, geo lookup has turned into a major bottleneck for the systems I am responsible for. Mostly because of license restrictions, but those reflect underlying technological limitations.

So this is intended to replace the geo lookup http server, and the design is supposed to be efficient for high volume geo lookups.

Note that you can deploy many instances of this server on the same machine (to take advantages of otherwise unused cpus and memory). To do this, using djb's supervise: create one directory for each virtual instance, and place in each a run script which hands control over to the real instance. For example:

    /supervise/geoh/ # the "concrete" instance
    /supervice/geoh01/run # A virtual instance
    /supervice/geoh02/run # A virtual instance
    /supervice/geoh03/run # A virtual instance
    /supervice/geoh04/run # A virtual instance
    /supervice/geoh05/run # A virtual instance
    /supervice/geoh06/run # A virtual instance
    /supervice/geoh07/run # A virtual instance
    /supervice/geoh08/run # A virtual instance
    /supervice/geoh09/run # A virtual instance
    /supervice/geoh10/run # A virtual instance
    /supervice/geoh11/run # A virtual instance
    /supervice/geoh12/run # A virtual instance
    /supervice/geoh13/run # A virtual instance
    /supervice/geoh14/run # A virtual instance
    /supervice/geoh15/run # A virtual instance
    /supervice/geoh16/run # A virtual instance

For this example, the executable (```chmod 755 run```) run scripts in the virtual instances would all look like this:

```sh
#!/bin/sh
exec /supervise/geoh/run
```

You can also deploy this on many machines if you use a load balancing front-end (but I have not managed to generate a load high enough to determine whether a load balancer is efficient enough for the multiple-machine approach to be useful).

Use:
---

for testing:

make; ./main 

For production, use http://cr.yp.to/daemontools.html, and:

* edit run, fixing definition for key
* chmod 4755 main
* then supervise the containing directory (see daemontool docs)

TODO:
----

Document tuning procedures.
Document technical choices.
Season to taste.

Structural overview:
-------------------

* refine-csv.ijs translates ip2location db3 data set to something which can be injested by buildmap
* buildmap translates ip-nub.csv (from refine-csv) to a map intended for use by main.c
* buildresponse translates country-state.csv to c to be incorporated into main
* main acts as an ip-lookup web server

