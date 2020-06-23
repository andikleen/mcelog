# mcelog

mcelog is the user space backend for logging machine check errors reported
by the hardware to the kernel. The kernel does the immediate actions
(like killing processes etc.) and mcelog decodes the errors and manages
various other advanced error responses like offlining memory, CPUs or triggering
events. In addition mcelog also handles corrected errors, by logging and
accounting them.
It primarily handles machine checks and thermal events, which are reported
for errors detected by the CPU.

For more details on what mcelog can do and the underlying theory
see [mcelog.org](https://www.mcelog.org).

It is recommended that mcelog runs on all x86 machines, both 64bit
(since early 2.6) and 32bit (since 2.6.32).

mcelog can run in several modes:

- cronjob
- trigger
- daemon

**cronjob** is the old method. mcelog runs every 5 minutes from cron and checks
for errors. Disadvantage of this is that it can delay error reporting
significantly (upto 10 minutes) and does not allow mcelog to keep extended state.

**trigger** is a newer method where the kernel runs mcelog on a error.

This is configured with:
```sh
echo /usr/sbin/mcelog > /sys/devices/system/machinecheck/machinecheck0/trigger
```
This is faster, but still doesn't allow mcelog to keep state,
and has relatively high overhead for each error because a program has
to be initialized from scratch.

In **daemon** mode mcelog runs continuously as a daemon in the background and
wait for errors. It is enabled by running `mcelog --daemon &`
from a init script. This is the fastest and most feature-ful.

The recommended mode is **daemon**, because several new functions (like page
error predictive failure analysis) require a continuously running daemon.

## Documentation

- The primary reference documentation are the man pages.
- [lk10-mcelog.pdf](lk10-mcelog.pdf)
  has a overview over the errors mcelog handles (originally from Linux Kongress 2010).
- [mce.pdf](mce.pdf)
  is a very old paper describing the first releases of mcelog (some parts are obsolete).

## For distributors

You can run mcelog from systemd or similar daemons. An example systemd unit
file is in `mcelog.service`.

By default mcelog reports its version as the git tag. This can be overridden
by setting up a `.os_version` file in the source directory. A build system
could write the OS version to this file to mark the binary.

### For older distributions using init scripts

Please install an init script by default that runs mcelog in daemon mode.
The `mcelog.init` script is a good starting point. Also install a
logrotated file (mcelog.logrotate) or equivalent when mcelog is running
in daemon mode.
These two are not in make install.

The installation also requires a config file `/etc/mcelog.conf` and the default
triggers. These are all installed by `make install`

`/dev/mcelog` is needed for mcelog operation. If it's not there it can be
created with:
```sh
mknod /dev/mcelog c 10 227
```

Normally it should be created automatically in udev.

## Security

mcelog needs to run as root because it might trigger actions like
page-offlining, which require `CAP_SYS_ADMIN`. Also it opens `/dev/mcelog`
and an UNIX socket for client support.

It also opens `/dev/mem` to parse the BIOS DMI tables. It is careful to close
the file descriptor and unmap any mappings after using them.

There is support for changing the user in daemon mode after opening the device
and the sockets, but that would stop triggers from doing corrective action
that require `root`.

In principle it would be possible to only keep `CAP_SYS_ADMIN` for page-offling,
but that would prevent triggers from doing root-only actions not covered by
it (and `CAP_SYS_ADMIN` is not that different from full root)

In `daemon` mode mcelog listens to a UNIX socket and processes requests from
`sh mcelog --client`. This can be disabled in the configuration file.
The uid/gid of the requestor is checked on access and is configurable
(default 0/0 only). The command parsing code is very straight forward
(server.c). The client parsing/reply is currently done with full privileges
of the `daemon`.

## Testing

There is a simple test suite in `sh tests/`. The test suite requires root to
run and access to mce-inject and a kernel with MCE injection support
`CONFIG_X86_MCE_INJECT`.  It will kill any running mcelog daemon.

Run it with `sh make test`.

The test suite requires the
[mce-inject](git://git.kernel.org/pub/utils/cpu/mce/mce-inject.git) tool.
The `mce-inject` executable must be either in `$PATH` or in the
`../mce-inject` directory.

You can also test under **valgrind** with `sh make valgrind-test`. For this
valgrind needs to be installed of course. Advanced valgrind options can be
specified with:
```sh
make VALGRIND="valgrind --option" valgrind-test
```

### Other checks

`make iccverify` and `make clangverify` run the static verifiers in *clang*
and *icc* respectively.

## License

This program is licensed under the subject of the GNU Public General
License, v.2
