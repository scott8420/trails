# Trails

A graphical system monitor for GNOME -- btop's depth with a modern, colourful
face. Tabs for CPU, memory, processes, disks, network, GPU, sensors & power and
services; actions on processes and services; and a **record** button that lays
down a trail you can follow back later, like a bloodhound.

gtkmm 4 / C++23. Started from the Cairn starter.

## Build, install, uninstall

```
./build.sh                    # build/trails and build/trails_selftest
./build/trails_selftest       # headless core tests
./install.sh                  # ~/.local (no sudo); --system, --prefix DIR, --build
./uninstall.sh                # same options; --purge also removes settings/cache
```

Deps (Fedora): `gtkmm4.0-devel spdlog-devel cmake gcc-c++` (optional: `ninja-build ccache mold`).
Deps (Ubuntu): `libgtkmm-4.0-dev libspdlog-dev cmake g++`.

## License

MIT
