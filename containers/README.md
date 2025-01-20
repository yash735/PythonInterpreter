Podman is a FOSS replacement for Docker.

The podman daemon is controlled by the `podman machine` commands.

* Do once, to get an initial vm:   `podman machine init`
* Whenever you need to use podman: `podman machine start`
* To stop the podman service:      `podman machine stop`


The file `~/.config/containers/containers.conf` must have this:

```
  [containers]
    tz = "local"
```

