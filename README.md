# IndietroTutta — Local build & upload

Quick commands to automate build and upload locally.

- **Setup virtualenv and install PlatformIO:**

```bash
make install
```

- **Build:**

```bash
make build
```

- **Upload:**

```bash
make upload
```

- **Monitor serial:**

```bash
make monitor
```

- **Auto watch (requires `entr`):**

```bash
make watch
```

- **Auto watch (alternative, requires `inotifywait`):**

```bash
./scripts/watch.sh
```

You can set the serial port with `PORT` environment variable, e.g.:

```bash
make upload PORT=/dev/ttyACM0
```
