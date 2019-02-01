MQLess
=======

MQLess is an actor model framework on top of AWS Lambda.

A good article on actor model https://www.brianstorti.com/the-actor-model/.

## Why Lambda

With most actor model frameworks you need to manage a complex cluster (e.g Akka).
Utilizing AWS Lambda we only need a component to route the requests to the actor.
MQLess is that component.

Also, most actor model frameworks are bounded to one language or framework.
MQLess is language and framework agnostic. 
You can implement your actors in any language that is supported by AWS Lambda.

## So what exactly MQLess do?

### Actors have mailboxes

> It’s important to understand that, although multiple actors can run at the same time, an actor will process a given message sequentially. This means that if you send 3 messages to the same actor, it will just execute one at a time. To have these 3 messages being executed concurrently, you need to create 3 actors and send one message to each. 

> Messages are sent asynchronously to an actor, that needs to store them somewhere while it’s processing another message. The mailbox is the place where these messages are stored.

MQLess is the mailboxes while AWS lambda is the actor.

MQLess sits between your API Gateway (or any other trigger you are using) and the lambda.
MQLess manage a mailbox per routing id (the unique idenifier of an actor).
With MQLess you will only have one lambda instance that handle a request for a routing key at a time.

## How to use MQLess

You first need to install the MQLess router in the cloud, have it as daemon in an EC2 instance.

### Installing MQLess in EC2 instance

You first need to create a user for mqless.
MQLess need to be able to invoke AWS lambdas, that the only requirement.

> MQLess doesn't support fetching the credentials from AWS metadata yet.

```
sudo apt install -y build-essential libcurl-dev libmicrohttpd-dev
git clone https://github.com/zeromq/libzmq.git
cd libzmq
./autogen.sh
./configure
make
sudo make install
cd ..
git clone https://github.com/zeromq/czmq.git
cd czmq
./autogen.sh
./configure
make
sudo make install
cd ..
git clone https://github.com/somdoron/mqless.git
cd mqless
./autogen.sh
./configure --with-systemd-units
make
sudo make install
sudo systemctl daemon-reload
```

Now open the configuration file of MQLess at `/usr/local/etc/mqless/mqless.cfg` and set your AWS credentials.
You can also set the port mqless will listen to.
Make sure to open the port in the Security Group.

You can now enable and start the service:

```
sudo systemctl enable mqless
sudo systemctl start mqless
```

Or you can just run it from shell `mqless`.

### Invoking a lambda using MQLess

To invoke a lambda you need to make HTTP POST to MQLess address with the url set to `/send/routing_key/function_name`.
The contnet of the POST request will be forwarded to the lambda.

Example with curl:

`curl --data '{"msg":"Hello"}' http://127.0.0.1:34543/send/123/hello`

### Handling the request with the lambda

When you invoke a lambda with MQLess, MQLess add the routing key to the payload of request. 
So the original payload will be under the `payload` field and the routing key will be in the `routing_key` field.
For example:

```json
{
  "routing_key": "123",
  "payload": {"msg":"Hello"}
}
```

The response you provide in the lambda will be forwarded back to the caller.


