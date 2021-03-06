Kernel Queue test module
========================

### BUILD HOWTO:
* Compile sources: `make`
* Load kernel module: `insmod ./kqueue.ko`
  
  you can use module arguments:

   1. `cache_storage`: file to cache queue messages; default value: `/tmp/kqueue-cache`
   2. `cache_async`: use async cache algorithm; default value: `0`
   
   When the module loaded, you can see at syslog: `kqueue: initialized properly (async: off)`
   
   ... and look into `/dev` directory: `ls -la /dev/kqueue*`
   
* Now, start pop daemon with root privilegies: `./kqueue-popd`
  
  to show available options: `./kqueue-popd --help`

* Finally, push something into queue. Run with root: `ls *.c | ./kqueue-push `

* Just check content of files: `popmsg-*`

* P.S. You can work with queue without userspace programs. Stop pop daemon & type something like this:
   
   1. `echo "first message"| sudo tee /dev/kqueue-push`
   2. `echo "second message"| sudo tee /dev/kqueue-push`
   3. `sudo cat /dev/kqueue-pop`
   4. `sudo cat /dev/kqueue-pop`
