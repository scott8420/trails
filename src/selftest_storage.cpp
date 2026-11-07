// selftest -- s007 storage + network readers, on an Asahi-shaped fixture:
// one NVMe, btrfs root + home subvolumes on p6, /boot ext4, /boot/efi vfat,
// a LUKS (dm-0) ext4 on p7 mounted at a path with a space, and the noise
// (a snap's squashfs on loop0, devtmpfs, tmpfs) that must stay out.
#include "core/Sampler.hpp"
#include "core/probe/Probe.hpp"

#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>

namespace core = trails::core;
namespace probe = core::probe;
namespace fs = std::filesystem;
using CheckFn = std::function<void(const std::string&, bool, const std::string&)>;

namespace {
struct Fx {
    fs::path root;
    Fx() {
        root = fs::temp_directory_path() /
               ("trails_st_" + std::to_string(::getpid()) + "_" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(root);
    }
    ~Fx() { std::error_code ec; fs::remove_all(root, ec); }
    void file(const std::string& rel, const std::string& text) const {
        fs::create_directories((root / rel).parent_path());
        std::ofstream(root / rel) << text;
    }
    void dir(const std::string& rel) const { fs::create_directories(root / rel); }
    std::string str() const { return root.string(); }
};
}  // namespace

void run_storage_tests(const CheckFn& check_fn) {
    auto check = [&](const std::string& n, bool ok, const std::string& d = "") { check_fn(n, ok, d); };
    std::cout << "\n-- storage + network (s007) --\n";

    Fx fx;
    fx.file("sys/block/nvme0n1/size", "1000215216\n");
    fx.file("sys/block/nvme0n1/queue/rotational", "0\n");
    fx.file("sys/block/nvme0n1/removable", "0\n");
    fx.file("sys/block/nvme0n1/device/model", "APPLE SSD AP0512R   \n");
    for (const char* p : {"nvme0n1p4", "nvme0n1p5", "nvme0n1p6", "nvme0n1p7"})
        fx.file(std::string("sys/block/nvme0n1/") + p + "/partition", "1\n");
    fx.dir("sys/block/nvme0n1/queue/iosched");   // not a partition
    fx.file("sys/block/dm-0/dm/name", "luks-abc\n");
    fx.file("sys/block/dm-0/slaves/nvme0n1p7", "");
    fx.file("sys/block/loop0/size", "0\n");
    fx.file("proc/self/mountinfo",
        "22 1 0:33 /root / rw,relatime shared:1 - btrfs /dev/nvme0n1p6 rw,seclabel,compress=zstd:1,ssd,subvolid=256,subvol=/root\n"
        "45 22 0:33 /home /home rw,relatime shared:2 - btrfs /dev/nvme0n1p6 rw,compress=zstd:1,ssd,subvolid=257,subvol=/home\n"
        "46 22 259:4 / /boot rw,relatime shared:3 - ext4 /dev/nvme0n1p5 rw\n"
        "47 46 259:3 / /boot/efi rw,relatime shared:4 - vfat /dev/nvme0n1p4 rw,fmask=0077\n"
        "48 22 253:0 / /mnt/My\\040Data ro,relatime shared:5 - ext4 /dev/mapper/luks-abc rw\n"
        "49 22 7:0 / /snap/core/1 ro shared:6 - squashfs /dev/loop0 ro\n"
        "50 22 0:5 / /dev rw shared:7 - devtmpfs devtmpfs rw\n"
        "51 22 0:40 / /tmp rw - tmpfs tmpfs rw\n"
        "garbage line\n");
    const std::string R = fx.str();

    std::vector<probe::Mount> ms;
    check("mountinfo: reads", probe::read_mounts(R, ms) && ms.size() == 8, std::to_string(ms.size()));
    if (ms.size() == 8) {
        check("mountinfo: btrfs subvol option", ms[0].subvol == "/root" && ms[1].subvol == "/home" && ms[1].root == "/home");
        check("mountinfo: \\040 decodes to a space", ms[4].point == "/mnt/My Data", ms[4].point);
        check("mountinfo: ro flag", ms[4].read_only && !ms[0].read_only);
        check("mountinfo: major:minor", ms[2].major == 259 && ms[2].minor == 4);
        check("storage: loop / devtmpfs / tmpfs are not storage",
              !probe::is_storage_mount(ms[5]) && !probe::is_storage_mount(ms[6]) && !probe::is_storage_mount(ms[7]));
    }

    check("dev: partition source", probe::dev_of_source(R, "/dev/nvme0n1p6") == "nvme0n1p6");
    check("dev: mapper name -> dm-0", probe::dev_of_source(R, "/dev/mapper/luks-abc") == "dm-0",
          probe::dev_of_source(R, "/dev/mapper/luks-abc"));
    check("under: partition -> disk", probe::disks_under(R, "nvme0n1p6") == std::vector<std::string>{"nvme0n1"});
    check("under: whole disk -> itself", probe::disks_under(R, "nvme0n1") == std::vector<std::string>{"nvme0n1"});
    check("under: dm -> slave -> disk", probe::disks_under(R, "dm-0") == std::vector<std::string>{"nvme0n1"});
    check("under: unknown -> nothing", probe::disks_under(R, "sdz9").empty());

    probe::DiskInfo di;
    check("disk info: reads", probe::read_disk_info(R, "nvme0n1", di));
    check("disk info: size from 512-byte sectors", di.size_bytes == 1000215216ull * 512);
    check("disk info: model trimmed, ssd", di.model == "APPLE SSD AP0512R" && !di.rotational && !di.removable, di.model);
    check("disk info: partitions only (not queue/)",
          di.parts == std::vector<std::string>{"nvme0n1p4", "nvme0n1p5", "nvme0n1p6", "nvme0n1p7"});
    check("disk info: absent disk", !probe::read_disk_info(R, "sdq", di));

    const auto fsv = core::group_filesystems(R, ms);
    check("fs: four filesystems (btrfs subvolumes fold into one)", fsv.size() == 4, std::to_string(fsv.size()));
    if (fsv.size() == 4) {
        const auto& root = fsv[0];
        check("fs: / first, btrfs", root.point == "/" && root.fstype == "btrfs", root.point);
        check("fs: two subvolumes, by mount point",
              root.subvols.size() == 2 && root.subvols[0] == std::pair<std::string, std::string>{"/root", "/"} &&
                  root.subvols[1] == std::pair<std::string, std::string>{"/home", "/home"});
        check("fs: btrfs lists subvolumes, not 'other mounts'", root.others.empty());
        check("fs: on nvme0n1", root.disks == std::vector<std::string>{"nvme0n1"});
        check("fs: usage read (the fixture dir)", root.usage.size > 0);
        check("fs: /boot, /boot/efi in order", fsv[1].point == "/boot" && fsv[2].point == "/boot/efi");
        const auto& luks = fsv[3];
        check("fs: LUKS ext4 on dm-0 -> nvme0n1", luks.dev == "dm-0" && luks.disks == std::vector<std::string>{"nvme0n1"});
        check("fs: missing mount point -> zero usage, no crash", luks.usage.size == 0 && luks.read_only);
    }
    {   // a bind mount of ext4: one filesystem, the extra point under `others`
        std::vector<probe::Mount> b;
        probe::parse_mountinfo("1 0 8:1 / /data rw - ext4 /dev/sda1 rw\n"
                               "2 0 8:1 /x /srv/x rw - ext4 /dev/sda1 rw\n", b);
        const auto v = core::group_filesystems(R, b);
        check("fs: bind mount folds, extra point kept", v.size() == 1 && v[0].point == "/data" &&
                                                            v[0].others == std::vector<std::string>{"/srv/x"});
    }

    // Network.
    fx.file("sys/class/net/wlan0/operstate", "up\n");
    fx.file("sys/class/net/wlan0/address", "a4:83:e7:00:11:22\n");
    fx.file("sys/class/net/wlan0/mtu", "1500\n");
    fx.dir("sys/class/net/wlan0/wireless");
    fx.file("sys/class/net/eth0/operstate", "down\n");
    fx.file("sys/class/net/eth0/speed", "1000\n");
    fx.file("sys/class/net/lo/operstate", "unknown\n");
    fx.dir("sys/devices/virtual/net/lo");
    fx.file("proc/net/wireless",
            "Inter-| sta-|   Quality        |   Discarded packets               | Missed | WE\n"
            " face | tus | link level noise |  nwid  crypt   frag  retry   misc | beacon | 22\n"
            " wlan0: 0000   54.  -56.  -256        0      0      0      0     21        0\n");
    probe::NetLink l;
    check("link: wlan0", probe::read_netlink(R, "wlan0", l) && l.wireless && !l.virtual_if &&
                             l.operstate == "up" && l.mtu == 1500 && l.speed_mbps == -1 && l.mac == "a4:83:e7:00:11:22");
    check("link: eth0 speed", probe::read_netlink(R, "eth0", l) && !l.wireless && l.speed_mbps == 1000 && l.operstate == "down");
    check("link: lo is virtual", probe::read_netlink(R, "lo", l) && l.virtual_if);
    check("link: absent", !probe::read_netlink(R, "nope0", l));
    std::vector<probe::Wireless> wl;
    check("wireless: one, headers skipped", probe::read_wireless(R, wl) && wl.size() == 1, std::to_string(wl.size()));
    if (wl.size() == 1)
        check("wireless: link 54, -56 dBm", wl[0].name == "wlan0" && wl[0].link == 54 && wl[0].level_dbm == -56);

    // The live system.
    {
        bool lo4 = false;
        for (const auto& a : probe::read_addrs())
            if (a.name == "lo" && !a.v6 && a.addr == "127.0.0.1" && a.prefix == 8) lo4 = true;
        check("live: getifaddrs finds 127.0.0.1/8 on lo", lo4);
        std::vector<probe::Mount> live;
        check("live: mountinfo reads", probe::read_mounts("/", live) && !live.empty(), std::to_string(live.size()));
    }
}
