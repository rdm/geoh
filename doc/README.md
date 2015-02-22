General design philosphy:
------------------------

Top priority is that this server needs to do what I want it to do. Figuring out how to make existing systems (e.g. apache+tomcat) behave properly turns out to be significantly more difficult than writing a server from scratch.

More generally, the internet is designed on the principle of "rough agreement and working code". There's a reason that the standards are only published in RFC (Request For Comments) form. Hypothetically speaking, this server could be thought of as violating some RFCs, but that can only happen in the context of unauthorized access which should not be happening in the first place.

My priorities are:

* Need to replace existing geolookup (my first use only cares about country code, but we use state code in some contexts).
* Need to serve requests efficiently (fast and with low resource consumption).

This means push complexity into the data, keep the code as simple as possible, especially in the main service loop

In other words, as much as possible, do preparation work "up front" so that it doesn't have to be repeated in the time-critical sections.

So the server is a state machine with a minimum of "moving parts".

Configuration is by editing the source files recompiling. Or, failing that, by talking to me.

Initial concept:
---------------

Geo lookup can be implemented as a memory mapped file. There are only 4 billion possible internet addresses, and our existing application only cares about 249 distinct country codes, so we could have a 4GB file enumerating these possibilities.

It turns out that there were a bit over 3000 distinct country+region codes in the ip2location database, so I went with an 8GB memory mapped file for my initial implementation. As there are 245 distinct country codes and I also want to support 52 distinct state codes, it just didn't make sense to try squeezing things down to one byte per entry. Most of the time, only a small number of ip addresses will be "live" so the extra bulk will mostly not need to live in memory.

Meanwhile, all I really need to do to implement the HTTP server is read from the client until I find a blank line, then I can look within the request for the parts that I care about. Most of the protocol is designed to deal with issues which do not matter for me.

So I run some small bits of code which generate the few thousand HTTP responses which can be valid for the geolookup results. Now all the server has to do is lookup the ip address in my mapped file to find an index, then send the response for that index to the client.

The most computationally intensive activity here has to do with the structure of the poll() system call. Documentation claims that poll() is more efficient than select(). I am mildly dubious about that, but I don't care enough to run benchmarks against optimized select code. Also note that the kernel has to do a fair amount of work for each request, so now that I've got the server down to a set of bare essentials I don't have to fret too much about further refinements.

That said, note that the very first implementation only had one response: 400 Bad Request. I didn't bother getting the geo ip lookup working until after I had the basic server mechanics implemented. I've since replaced that error response with 403 Forbidden to reflect the philosophy that 99.9999995% of the world's population should not be talking with this server.

HTTP Pipelining:
---------------

I really did not want to implement support for HTTP pipelining. HTTP pipelining easily doubles the complexity of my server. However, the underlying TCP protocol has some inefficiencies which mean that the network overhead of not using HTTP pipelining mcan more than outweigh the server inefficiencies. Also, my server is pretty simple, so "double the complexity" isn't all that complex.

But also, I wanted the server to give me some information about the traffic it was serving, and the timeouts that HTTP pipelining needs are similar in structure to the timeouts needed for pipelining. So I was wanting to put some of that complexity in place anyways.

A related issue might be Denial Of Service attacks. Ideally, you want your software to have some immunity to DOS attacks. But at some point that becomes a fig-leaf and you have to rely on other people being well behaved enough (at least, statistically speaking) that you can ignore these attacks. (That won't always be the case, but nothing can ever be perfect.)

So I implemented a timeout only HTTP/1.1 pipelining mechanism. It'll also shut down on invalid requests (such as not providing the right key).

It turned out, though, that the existing client was way more inefficient than I thought possible. When I had the pipeline timeout set at 10 seconds, almost all of the requests were failing because the client would not send the request fast enough. (For contrast, the tcp overhead I was worrying about might eat less than a millisecond within the amazon us-east compartment.)

So I've bumped the timeout up to 100 seconds which lets most requests (but not all!) finish before the timeout expires.

I'll have to do some work on the client, also, of course...

Portability:
-----------

I have tried to make this as portable as possible. I developed the original versions on OpenBSD, and later versions on my work laptop (OS X Yosemite), and am deploying on Linux. I'll be wanting to test changes on OpenBSD on an ongoing basis. (Anything that doesn't work on all three of these operating systems should be rejected.)

Logging:
-------

I really am only interested in summary information, so I attempt to count the number of requests. "Good" requests are requests which get a geo-ip response. "Bad" requests are requests which get an error response, or which I have to close without sending a "Good" response. The decision about whether a request is good or bad has to happen before the request is complete, but I can only count a request as Good after it has been delivered, so I also report on how many requests are in-progress. 

Or that was the original idea... but think about pipelining for a moment. Once I start sending pipeline responses, I leave the connection open after sending each response. If I then close that connection, that's not really an error - that's just how the system works. So I also have a concept of a "Neutral" request state. I just ignore things like timeouts and connection close on a socket that's "Neutral".

But, also, when I tested this, I found that I was getting a lot of "Bad" requests which really never went anywhere. The client just opened the connection and then sat there without sending anything. But this could conceal other problems, so I count these as "Empty" instead of "Bad". "Empty" is really almost the same thing as "Neutral", except it's the initial state of a socket, before anything is received. (Once something is received the socket gets classified as "Good" or "Bad". Once the response is sent, the socket is either closed or placed into the "Neutral" state.)

I also log how many connections are "Pending" - this means tcp sockets in the connected state. A logged line then looks something like this:

    Sun Feb 22 21:31:20 2015: NEW good: 1597, empty: 69, bad: 0, PENDING 493/499, TOTAL good: 7173, empty: 337, bad: 0

The second number after PENDING is the number of readfd entries I'm holding to represent connections. This is an artifact of the design of poll() -- and note that I am assuming that readers of this documentation will be looking up each technical term that they are not familiar with, and I do understand that that will initially be frustrating -- poll() wants an array of struct pollfd and for this kind of server we manage that array in two ways:

* we mark the end of the array as being after the last used entry (the actual array is 1024 entries long), and
* we mark unused entries with -1 for the file descriptor.

So, in the above log line example, 499 is the index of the last valid struct pollfd entry. (This means that there are actually 500 entries in the array which poll has to search... but note that the very first entry is the listening socket which will accept new connections from clients - since that one is special and never gets any requests I don't include it in my log counts).


