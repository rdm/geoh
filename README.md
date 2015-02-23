# geoh
geo lookup tools

Current status: working

Why:
---

In my current job, geo lookup has turned into a major bottleneck for the systems I am responsible for. Mostly because of license restrictions, but those reflect underlying technological limitations.

So this is intended to replace the geo lookup http server, and the design is supposed to be efficient for high volume geo lookups.

Note that you can deploy many instances of this server on the same machine (to take advantages of otherwise unused cpus and memory). To do this, using djb's supervise: create one directory for each virtual instance, and place in each a run script which hands control over to the real instance. For example:

    /usr/local/src/geoh/      # the "concrete" instance
    /etc/service/geoh01/run   # A virtual instance
    /etc/service/geoh02/run   # A virtual instance
    /etc/service/geoh03/run   # A virtual instance
    /etc/service/geoh04/run   # A virtual instance
    /etc/service/geoh05/run   # A virtual instance
    /etc/service/geoh06/run   # A virtual instance
    /etc/service/geoh07/run   # A virtual instance
    /etc/service/geoh08/run   # A virtual instance
    /etc/service/geoh09/run   # A virtual instance
    /etc/service/geoh10/run   # A virtual instance
    /etc/service/geoh11/run   # A virtual instance
    /etc/service/geoh12/run   # A virtual instance
    /etc/service/geoh13/run   # A virtual instance
    /etc/service/geoh14/run   # A virtual instance
    /etc/service/geoh15/run   # A virtual instance
    /etc/service/geoh16/run   # A virtual instance

For this example, the executable (```chmod 755 run```) run scripts in the virtual instances would all look like this:

```sh
#!/bin/sh
exec /usr/local/src/geoh/start
```

You can also deploy this on many machines if you use a load balancing front-end (but I have not managed to generate a load high enough to determine whether a load balancer is efficient enough for the multiple-machine approach to be useful).

Use:
---

for testing:

make; ./main 

For production, use http://cr.yp.to/daemontools.html, and create a script named 'start' which looks something like this:

```sh
#!/bin/sh
exec /usr/local/src/geoh KEYGOESHERE
```

Also:
* cp main geoh
* sudo chown root geoh; sudo chmod u+s geoh
* use supervise to run this (see above)

Structural overview:
-------------------

* refine-csv.ijs translates ip2location db3 data set to something which can be injested by buildmap
* buildmap translates ip-nub.csv (from refine-csv) to a map intended for use by main.c
* buildresponse translates country-state.csv to c to be incorporated into main
* main acts as an ip-lookup web server

