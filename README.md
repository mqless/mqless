MQLess
=======

MQLess is an actor model framework on top of AWS Lambda.

A good article on actor model https://www.brianstorti.com/the-actor-model/.

## Why Lambda

With most actor model frameworks you need to manage a complex cluster.
Utilizing AWS Lambda we only need a component to route the requests to the actor.
MQLess is that component.

Also, most actor model frameworks are bounded to one language or framework.
MQLess is language and framework agnostic. 
You can implement your actors in any language that is supported by AWS Lambda.

## So what exactly MQLess do?

### Actors have mailboxes

> It’s important to understand that, although multiple actors can run at the same time, an actor will process a given message sequentially. This means that if you send 3 messages to the same actor, it will just execute one at a time. To have these 3 messages being executed concurrently, you need to create 3 actors and send one message to each. 

> Messages are sent asynchronously to an actor, that needs to store them somewhere while it’s processing another message. The mailbox is the place where these messages are stored.
 

MQLess is the mailboxes 

MQLess sits between your API Gateway (or any other trigger you are using)

