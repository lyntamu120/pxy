#ECEN 602
#assignment - 4
#Group - 7
@Author - Xining Wang(525004553) and Huiqing Wang(927001110)

This project creates a http proxy and a single client. The http proxy server uses a LRU cache to cache the most recently used 10 files,
and store them in the current directory.

Features of this http proxy application :
1. The proxy binds the local ip and a port to start listening for connections.
2. Several client can connect to the server at the IP PORT the proxy is listening to with a url.
3. Once the server receives the request, it first check its cache
    a. If cache contains the file, check the expire or date and last-modified header
        i. If it's stale, send a Conditional GET to the server, if receives a 304, it will send the cached file to the client. If it receives a 200, just update the cached file and send it to the client
        ii. If it's fresh, it will send the file to the client
    b. If cache doesn't contain the file, the proxy server will send a normal GET request to the web
4. The server can also support multiple users. If multiple users send the same url to the proxy, the proxy will first handle the first
request and then handle the rest.

Running the application:
1. Locate the httpProxy folder, type in 'make'
2. Locate the client folder, type in 'make'
3. On the httpProxy folder directory, type in './proxy 127.0.0.1 5432'
4. On the client directory, type in './client 127.0.0.1 5432 www.tamu.edu/index.html'
5. The proxy will handle the request and send the file to the client
