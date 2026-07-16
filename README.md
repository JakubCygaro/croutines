# C coroutines

A simple experiment in creating C coroutines with message passing and longjmp.

A couroutine can `send()` a message (`char[128]`) to a different couroutine
with its ID. It will then get blocked untill the message is delivered. A couroutine
can also `recv()` a message, this action can block it if there is no pending message
delivered. That couroutine will be blocked untill a message is sent to it.
