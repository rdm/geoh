# geoh
geo lookup tools

Current status: working

Why:
---

In my current job, geo lookup has turned into a major bottleneck for the systems I am responsible for. Mostly because of license restrictions, but those reflect underlying technological limitations.

So this is intended to replace the geo lookup http server, and the design is supposed to be efficient for high volume geo lookups.

Note that you can deploy many instances of this server on the same machine (to take advantages of otherwise unused cpus and memory). To do this, using djb's supervise: create one directory for each virtual instance, and place in each a run script which hands control over to the real instance. For example:

    /usr/local/src/geoh/      # the "concrete" instance
    /service/geoh01/run       # A virtual instance
    /service/geoh02/run       # A virtual instance
    /service/geoh03/run       # A virtual instance
    /service/geoh04/run       # A virtual instance
    /service/geoh05/run       # A virtual instance
    /service/geoh06/run       # A virtual instance
    /service/geoh07/run       # A virtual instance
    /service/geoh08/run       # A virtual instance
    /service/geoh09/run       # A virtual instance
    /service/geoh10/run       # A virtual instance
    /service/geoh11/run       # A virtual instance
    /service/geoh12/run       # A virtual instance
    /service/geoh13/run       # A virtual instance
    /service/geoh14/run       # A virtual instance
    /service/geoh15/run       # A virtual instance
    /service/geoh16/run       # A virtual instance

Note that if you are using ubuntu's implementation of daemontools you'll probably have to make adjustments which depend on the version of ubuntu which you are using. For example, in a current version of ubuntu the run script must be placed in /var/lib/supervise/geoh\*/ and /etc/service/geoh\* is meant to be a symbolic link to that directory.

For this example, the executable (```chmod 755 run```) run scripts in the virtual instances would all look something like this:

```sh
#!/bin/sh
set -e
exec 2>&1
cd /usr/local/src/
exec /usr/local/src/start
```

Of course, adjust the directory referenced here, based on where you have deployed the software.

Additionally, for logging purposes, you'll probably want a directory at /service/geoh\*/log/ which contains a run script something like this:

```sh
#!/bin/sh
chdir $(dirname $0)
exec setuidgid syslog multilog t ./main
```

This particular script needs an additional directory at /service/geoh\*/log/main/ which is owned by the syslog user.

Note that svscan likes the machine to be rebooted after major changes. (For example: it's better to start svcscan on boot than from the command line.)

Once this is in place, deploying an update to production looks something like this:

```sh
#!/bin/sh
set -e
make
sudo chown root main
sudo chmod r+s main
sudo mv main geoh
sudo svc -t /service/geoh*
sleep 1
sudo svstat /service/geoh*
echo $(pgrep geoh | wc -l) running server instances
```

(You'll want to run make and run main from the command line and run a test against it before doing this.)

Hypothetically speaking, you could deploy this server on many machines if you use a load balancing front-end (but I have not managed to generate a load high enough to determine whether a load balancer is efficient enough for the multiple-machine approach to be useful).

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

